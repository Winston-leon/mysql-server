#include "sql/sched_affinity_manager.h"
#ifdef HAVE_LIBNUMA
#include <numa.h>
#endif
#include <string>
#include <iostream>
#include "gtest/gtest.h"
#include "my_config.h"

#ifdef HAVE_LIBNUMA

using ::sched_affinity::Sched_affinity_manager;
using ::sched_affinity::Sched_affinity_manager_numa;
using ::sched_affinity::Thread_type;
using ::testing::TestInfo;
using namespace std;

class SchedAffinityManagerTest : public ::testing::Test {
 protected:
  bool skip_if_numa_unavailable() {
    if (numa_available() == -1) {
      SUCCEED() << "Skip test case as numa is unavailable.";
      return true;
    } else {
      return false;
    }
  }

  std::string convert_bitmask_to_string(struct bitmask* bitmask, int& min, int& max) {
    std::string res;

    int i = 0, total_cpu_num = numa_num_configured_cpus();
    bool flag1 = false, flag2 = false;
    for (; i < total_cpu_num; ) {
      int start = i, curr = i;

      while (curr < total_cpu_num 
             && numa_bitmask_isbitset(bitmask, curr)) {
        max = curr;
        curr++;
        flag2 = true;
      }

      if(flag1 && flag2) {
        res += ",";
      }

      if(curr != start && !flag1) {
        flag1 = true;
        min = start;
      }

      if (curr != start && curr - start == 1) {
        res += std::to_string(start);
      } else if (curr != start) {
        res += std::to_string(start) + "-" + std::to_string(curr-1);
      }
      if (curr == start) {
        i++;
      } else {
        i += (curr - start);
      }

      flag2 = false;
    }

    return res;
  }

  // bool numa_sched_conflict_precheck(struct bitmask *test_bitmask, int cpu_num) {
  //   struct bitmask *process_bitmask = numa_allocate_cpumask();
  //   int ret = numa_sched_getaffinity(0, process_bitmask);

  //   if (ret == -1) {
  //     return true;
  //   } else {
  //     bool flag = false;
  //     for (int i = 0; i < cpu_num; i++) {
  //       if (numa_bitmask_isbitset(test_bitmask, i) 
  //           && !numa_bitmask_isbitset(process_bitmask, i)) {
  //         flag = true;
  //         break;
  //       }
  //     }
  //     if (flag) {
  //       SUCCEED() << "Skip test case as the subsequent cpu sched affinity conflicts with the process cpu affinity.";
  //       return true;
  //     }
  //   }
  //   numa_free_cpumask(process_bitmask);
  //   process_bitmask = nullptr;

  //   return false;
  // }

  bool check_static_bind(int index) {
    struct bitmask *sched_bitmask = numa_allocate_cpumask();
    int ret = numa_sched_getaffinity(0, sched_bitmask);

    if (ret != -1) {
      bool flag = false;
      for (int i = 0; i < numa_num_configured_cpus(); i++) {
        if ((i == index && !numa_bitmask_isbitset(sched_bitmask, i)) 
            || (i != index && numa_bitmask_isbitset(sched_bitmask, i))) {
          flag = true;
          break;
        }
      }
      if (flag) {
        return false;
      }
    } else {
      return false;
    }

    numa_free_cpumask(sched_bitmask);
    sched_bitmask = nullptr;
    return true;
  }

  int get_avail_node_num(int node_num) {
    int res = 0;
    struct bitmask *bm = numa_get_run_node_mask();

    for (int i = 0; i < node_num; i++) {
      if (numa_bitmask_isbitset(bm, i)) {
        res++;
      }
    }

    return res;
  }

  // void init_avail_nodes(char nodes_arr[], int node_num) {
  //   struct bitmask *bm = numa_get_run_node_mask();

  //   for (int i = 0, arr_index = 0; i < node_num; i++) {
  //     if (numa_bitmask_isbitset(bm, i)) {
  //       nodes_arr[arr_index++] = i;
  //     }
  //   }
  // }

 protected:
  void SetUp() {
    if (skip_if_numa_unavailable()) {
      return;
    } else {
      test_process_cpu_num = numa_num_configured_cpus();
      test_process_node_num = numa_num_configured_nodes();
      test_avail_node_num = get_avail_node_num(test_process_node_num);

      nodes_arr.resize(test_avail_node_num);

      struct bitmask *test_process_node_mask = numa_get_run_node_mask();

      for (int i = 0, arr_index = 0; i < test_process_node_num; i++) {
        if (numa_bitmask_isbitset(test_process_node_mask, i)) {
          nodes_arr[arr_index++] = i;
        }
      }
      
      default_bitmask = numa_allocate_cpumask();
      numa_sched_getaffinity(0, default_bitmask);

      cpu_range_str = convert_bitmask_to_string(default_bitmask, cpu_range_min, cpu_range_max);
      // init_avail_nodes(nodes_arr, test_process_node_num);

      default_config = {
        {sched_affinity::Thread_type::FOREGROUND, nullptr},
        {sched_affinity::Thread_type::LOG_WRITER, nullptr},
        {sched_affinity::Thread_type::LOG_FLUSHER, nullptr},
        {sched_affinity::Thread_type::LOG_WRITE_NOTIFIER, nullptr},
        {sched_affinity::Thread_type::LOG_FLUSH_NOTIFIER, nullptr},
        {sched_affinity::Thread_type::LOG_CLOSER, nullptr},
        {sched_affinity::Thread_type::LOG_CHECKPOINTER, nullptr},
        {sched_affinity::Thread_type::PURGE_COORDINATOR, nullptr}};
    }
  }

  void TearDown() {
    numa_sched_setaffinity(0, default_bitmask);

    numa_free_cpumask(default_bitmask);
    default_bitmask = nullptr;
  }

 protected:
  int test_process_cpu_num;

  int test_process_node_num;

  int test_avail_node_num;

  int cpu_range_min;

  int cpu_range_max;

  std::vector<char> nodes_arr;

  std::string cpu_range_str;

  const int BUFFER_SIZE_1024 = 1024;

  std::map<Thread_type, char *> default_config;

 private:
  struct bitmask *default_bitmask;
};

TEST_F(SchedAffinityManagerTest, DefaultConfig) {
  if (skip_if_numa_unavailable()) {
    return;
  }

  auto instance = Sched_affinity_manager::create_instance(default_config);
  ASSERT_NE(instance, nullptr);
  ASSERT_EQ(typeid(*instance), typeid(Sched_affinity_manager_numa));

  ASSERT_TRUE(instance->get_total_node_number() > 0);
  ASSERT_TRUE(instance->get_cpu_number_per_node() > 0);

  Sched_affinity_manager::free_instance();
}

TEST_F(SchedAffinityManagerTest, ErrorFormatConfig) {
  if (skip_if_numa_unavailable()) {
    return;
  }

  /* Blank space at the beginning of string */
  std::string test_str = " " + cpu_range_str;
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_str.c_str());

  auto instance = Sched_affinity_manager::create_instance(default_config);
  EXPECT_NE(instance, nullptr);

  Sched_affinity_manager::free_instance();

  /* Blank space at the end of string */
  test_str = cpu_range_str + " ";
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_str.c_str());
  instance = Sched_affinity_manager::create_instance(default_config);
  EXPECT_EQ(instance, nullptr);

  Sched_affinity_manager::free_instance();

  /* Blank space in the middle of string */
  test_str = std::to_string(cpu_range_min) + " ," + std::to_string(cpu_range_max);
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_str.c_str());
  instance = Sched_affinity_manager::create_instance(default_config);
  EXPECT_EQ(instance, nullptr);

  Sched_affinity_manager::free_instance();

  /* Cpu range cross the border */
  test_str = cpu_range_str + "," + std::to_string(cpu_range_max + 1);
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_str.c_str());
  instance = Sched_affinity_manager::create_instance(default_config);
  EXPECT_EQ(instance, nullptr);

  Sched_affinity_manager::free_instance();
}

TEST_F(SchedAffinityManagerTest, ThreadProcessConflictConfig) {
  if (skip_if_numa_unavailable()) {
    return;
  }
  
  std::string test_str = std::to_string(cpu_range_min);
  struct bitmask *test_process_bitmask = numa_parse_cpustring(test_str.c_str());
  numa_sched_setaffinity(0, test_process_bitmask);

  if (cpu_range_min != cpu_range_max) {
    test_str = std::to_string(cpu_range_max);
  } else if (cpu_range_min < test_process_cpu_num - 1) {
    test_str = std::to_string(cpu_range_min + 1);
  } else {
    test_str = std::to_string(cpu_range_min -1);
  }
  
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_str.c_str());
  auto instance = Sched_affinity_manager::create_instance(default_config);
  EXPECT_EQ(instance, nullptr);

  numa_free_cpumask(test_process_bitmask);
  test_process_bitmask = nullptr;
  Sched_affinity_manager::free_instance();
}

TEST_F(SchedAffinityManagerTest, ForegroundBackgroundConflictConfig) {
  if (skip_if_numa_unavailable()) {
    return;
  }
  
  std::string test_fore_str = cpu_range_str;
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_fore_str.c_str());

  std::string test_back_str = cpu_range_str;
  default_config[sched_affinity::Thread_type::LOG_WRITER] = const_cast<char *>(test_back_str.c_str());

  auto instance = Sched_affinity_manager::create_instance(default_config);
  /* Background conflicting with foreground doesnt stop the instantiation, but pop out a warning */
  EXPECT_NE(instance, nullptr);

  Sched_affinity_manager::free_instance();
}

TEST_F(SchedAffinityManagerTest, DynamicBind) {
  if (skip_if_numa_unavailable()) {
    return;
  }
  
  std::string test_str = cpu_range_str;
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_str.c_str());

  auto instance = Sched_affinity_manager::create_instance(default_config);
  ASSERT_NE(instance, nullptr);

  int total_node_num = numa_num_configured_nodes();
  int total_cpu_num = numa_num_configured_cpus();
  int cpu_num_per_node = total_cpu_num / total_node_num;

  for (int i = 0; i < test_process_cpu_num + 1; i++) {
    int group_index = -1;
    EXPECT_EQ(Sched_affinity_manager::get_instance()->dynamic_bind(group_index), true);
    EXPECT_EQ(group_index, nodes_arr[i % test_avail_node_num]);
    struct bitmask *bm = numa_allocate_cpumask();
    int ret = numa_sched_getaffinity(0, bm);
    ASSERT_NE(ret, -1);
    for (int j = 0; j < test_process_cpu_num; j++) {
      if (j >= group_index * cpu_num_per_node 
          && j < (group_index + 1) * cpu_num_per_node) {
        EXPECT_EQ(numa_bitmask_isbitset(bm, j), 1);
      } else {
        EXPECT_EQ(numa_bitmask_isbitset(bm, j), 0);
      }
    }
    numa_free_cpumask(bm);
    bm = nullptr;
  }

  Sched_affinity_manager::free_instance();
}

TEST_F(SchedAffinityManagerTest, DynamicUnbind) {
  if (skip_if_numa_unavailable()) {
    return;
  }
  
  std::string test_str = cpu_range_str;
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_str.c_str());

  auto instance = Sched_affinity_manager::create_instance(default_config);
  ASSERT_NE(instance, nullptr);

  int group_index_nobind = -1;
  EXPECT_EQ(Sched_affinity_manager::get_instance()->dynamic_unbind(group_index_nobind), false);

  int group_index_excessive = test_process_node_num;
  EXPECT_EQ(Sched_affinity_manager::get_instance()->dynamic_unbind(group_index_excessive), false);

  int group_index = -1;
  EXPECT_EQ(Sched_affinity_manager::get_instance()->dynamic_bind(group_index), true);
  EXPECT_EQ(group_index, nodes_arr[0]);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->dynamic_unbind(group_index), true);

  Sched_affinity_manager::free_instance();
}

TEST_F(SchedAffinityManagerTest, StaticBind) {
  if (skip_if_numa_unavailable()) {
    return;
  }
  
  auto instance = Sched_affinity_manager::create_instance(default_config);
  ASSERT_NE(instance, nullptr);

  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::FOREGROUND), false);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_WRITER), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_FLUSHER), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_WRITE_NOTIFIER), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_FLUSH_NOTIFIER), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_CLOSER), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_CHECKPOINTER), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::PURGE_COORDINATOR), true);

  Sched_affinity_manager::free_instance();

  default_config[sched_affinity::Thread_type::LOG_WRITER] = const_cast<char *>(std::to_string(cpu_range_min).c_str());
  default_config[sched_affinity::Thread_type::LOG_FLUSHER] = const_cast<char *>(std::to_string(cpu_range_max).c_str());
  default_config[sched_affinity::Thread_type::LOG_WRITE_NOTIFIER] = const_cast<char *>(std::to_string(cpu_range_min).c_str());
  default_config[sched_affinity::Thread_type::LOG_FLUSH_NOTIFIER] = const_cast<char *>(std::to_string(cpu_range_max).c_str());
  default_config[sched_affinity::Thread_type::LOG_CLOSER] = const_cast<char *>(std::to_string(cpu_range_min).c_str());
  default_config[sched_affinity::Thread_type::LOG_CHECKPOINTER] = const_cast<char *>(std::to_string(cpu_range_max).c_str());
  default_config[sched_affinity::Thread_type::PURGE_COORDINATOR] = const_cast<char *>(std::to_string(cpu_range_min).c_str());

  instance = Sched_affinity_manager::create_instance(default_config);
  ASSERT_NE(instance, nullptr);

  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::FOREGROUND), false);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_WRITER), true);
  EXPECT_EQ(check_static_bind(cpu_range_min), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_FLUSHER), true);
  EXPECT_EQ(check_static_bind(cpu_range_max), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_WRITE_NOTIFIER), true);
  EXPECT_EQ(check_static_bind(cpu_range_min), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_FLUSH_NOTIFIER), true);
  EXPECT_EQ(check_static_bind(cpu_range_max), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_CLOSER), true);
  EXPECT_EQ(check_static_bind(cpu_range_min), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_CHECKPOINTER), true);
  EXPECT_EQ(check_static_bind(cpu_range_max), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::PURGE_COORDINATOR), true);
  EXPECT_EQ(check_static_bind(cpu_range_min), true);

  Sched_affinity_manager::free_instance();
}

TEST_F(SchedAffinityManagerTest, TakeSnapshot) {
  if (skip_if_numa_unavailable()) {
    return;
  }
  
  std::string test_str = cpu_range_str;
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_str.c_str());
  auto instance = Sched_affinity_manager::create_instance(default_config);
  ASSERT_NE(instance, nullptr);

  /* test the case when the buff param is a null pointer */
  char *buff = nullptr;
  int buff_size = BUFFER_SIZE_1024;

  Sched_affinity_manager::get_instance()->take_snapshot(buff, buff_size);
  EXPECT_EQ(buff, nullptr);

  int cpu_num_per_node = test_process_cpu_num / test_process_node_num;

  buff = new char[BUFFER_SIZE_1024];

  ASSERT_NE(buff, nullptr);

  int initial_group_index = -1;
  Sched_affinity_manager::get_instance()->dynamic_bind(initial_group_index);

  std::string criterion_str;
  std::string buffered_str;
  for (int i = 0; i < test_process_node_num; i++) {
    if (i == nodes_arr[0]) {
      criterion_str += "1/" + std::to_string(cpu_num_per_node);
    } else {
      criterion_str += "0/" + std::to_string(cpu_num_per_node);
    }
    criterion_str += "; ";
  }

  /* test the case when the buff_size param is negative */
  buff_size = -2;
  Sched_affinity_manager::get_instance()->take_snapshot(buff, buff_size);
  buffered_str = std::string(buff);
  EXPECT_NE(criterion_str, buffered_str);

  /* test the case when the params are both normal*/
  buff_size = BUFFER_SIZE_1024;
  Sched_affinity_manager::get_instance()->take_snapshot(buff, buff_size);
  buffered_str = std::string(buff);
  EXPECT_EQ(criterion_str, buffered_str);

  buff = nullptr;
  Sched_affinity_manager::free_instance();
}

TEST_F(SchedAffinityManagerTest, GetTotalNodeNumber) {
  if (skip_if_numa_unavailable()) {
    return;
  }
  
  auto instance = Sched_affinity_manager::create_instance(default_config);
  ASSERT_NE(instance, nullptr);

  int criterion = numa_num_configured_nodes();
  EXPECT_EQ(Sched_affinity_manager::get_instance()->get_total_node_number(), criterion);

  Sched_affinity_manager::free_instance();
}

TEST_F(SchedAffinityManagerTest, GetCpuNumberPerNode) {
  if (skip_if_numa_unavailable()) {
    return;
  }
  
  auto instance = Sched_affinity_manager::create_instance(default_config);
  ASSERT_NE(instance, nullptr);

  auto criterion = numa_num_configured_cpus() / numa_num_configured_nodes();
  EXPECT_EQ(Sched_affinity_manager::get_instance()->get_cpu_number_per_node(), criterion);

  Sched_affinity_manager::free_instance();
}

#endif /* HAVE_LIBNUMA */