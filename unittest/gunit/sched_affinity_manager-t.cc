#include "gtest/gtest.h"
#include "my_config.h"
#include "sql/sched_affinity_manager.h"
#include <numa.h>
#include <malloc.h>
#include <string>

#ifdef HAVE_LIBNUMA

using ::sched_affinity::Sched_affinity_manager;
using ::sched_affinity::Sched_affinity_manager_numa;
using ::sched_affinity::Thread_type;
using ::testing::TestInfo;
using namespace std;

class SchedAffinityManagerTest : public ::testing::Test {
 protected:
  int test_process_cpu_num;

  std::map<Thread_type, char *> default_config;

  void get_overlong_str(char *str, int len) {
    str[len] = '\0';
  }

  bool skip_if_numa_unavailable() {
    if (numa_available() == -1) {
      SUCCEED() << "Skip test case as numa is unavailable.";
      return true;
    } else {
      return false;
    }
  }

 protected:
  void SetUp() {
    test_process_cpu_num = numa_num_task_cpus();

    std::string str = "0-" + std::to_string(test_process_cpu_num - 1);
    default_bitmask = numa_parse_cpustring(str.c_str());
    numa_sched_setaffinity(0, default_bitmask);

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
  void TearDown() {
    numa_free_cpumask(default_bitmask);
    default_bitmask = nullptr;
  }

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
}

TEST_F(SchedAffinityManagerTest, ErrorFormatConfig) {
  if (skip_if_numa_unavailable()) {
    return;
  }
  
  /* Blank space at the beginning of string */
  std::string test_str = " 0-" + std::to_string(test_process_cpu_num - 1);
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_str.c_str());

  auto instance = Sched_affinity_manager::create_instance(default_config);
  EXPECT_NE(instance, nullptr);

  Sched_affinity_manager::free_instance();

  /* Blank space at the end of string */
  test_str = "0-" + std::to_string(test_process_cpu_num - 1) + " ";
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_str.c_str());
  instance = Sched_affinity_manager::create_instance(default_config);
  EXPECT_EQ(instance, nullptr);

  Sched_affinity_manager::free_instance();

  /* Blank space in the middle of string */
  test_str = "0 - " + std::to_string(test_process_cpu_num - 1);
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_str.c_str());
  instance = Sched_affinity_manager::create_instance(default_config);
  EXPECT_EQ(instance, nullptr);

  Sched_affinity_manager::free_instance();
  
  char buff[1024+1];
  default_config[sched_affinity::Thread_type::FOREGROUND] = buff;
  /* Overlong string */
  get_overlong_str(default_config[sched_affinity::Thread_type::FOREGROUND], 1024);
  instance = Sched_affinity_manager::create_instance(default_config);
  EXPECT_EQ(instance, nullptr);

  Sched_affinity_manager::free_instance();
}

TEST_F(SchedAffinityManagerTest, ThreadProcessConflictConfig) {
  if (skip_if_numa_unavailable()) {
    return;
  }

  std::string test_str = "0-" + std::to_string(test_process_cpu_num / 2);
  struct bitmask *test_process_bitmask = numa_parse_cpustring(test_str.c_str());
  numa_sched_setaffinity(0, test_process_bitmask);

  test_str = "0-" + std::to_string(test_process_cpu_num - 1);
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_str.c_str());
  auto instance = Sched_affinity_manager::create_instance(default_config);
  EXPECT_EQ(instance, nullptr);

  Sched_affinity_manager::free_instance();
}

TEST_F(SchedAffinityManagerTest, ForegroundBackgroundConflictConfig) {
  if (skip_if_numa_unavailable()) {
    return;
  }

  std::string test_fore_str = "0-" + std::to_string(test_process_cpu_num / 2);
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_fore_str.c_str());

  std::string test_back_str = std::to_string(test_process_cpu_num / 2) + "-" + std::to_string(2 * test_process_cpu_num / 3);
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

  std::string test_str = "0-" + std::to_string(test_process_cpu_num - 1);
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_str.c_str());

  auto instance = Sched_affinity_manager::create_instance(default_config);
  ASSERT_NE(instance, nullptr);

  int total_node_num = numa_num_configured_nodes();

  for (int i = 0; i <= test_process_cpu_num; i++) {
    int initial_index = -1;
    EXPECT_EQ(Sched_affinity_manager::get_instance()->dynamic_bind(initial_index), true);
    EXPECT_EQ(initial_index, i % total_node_num);
  }

  Sched_affinity_manager::free_instance();
}

TEST_F(SchedAffinityManagerTest, DynamicUnbind) {
  if (skip_if_numa_unavailable()) {
    return;
  }

  std::string test_str = "0-" + std::to_string(test_process_cpu_num - 1);
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_str.c_str());

  auto instance = Sched_affinity_manager::create_instance(default_config);
  ASSERT_NE(instance, nullptr);

  int initial_index_nobind = -1;
  EXPECT_EQ(Sched_affinity_manager::get_instance()->dynamic_unbind(initial_index_nobind), false);

  int initial_index_excessive = numa_num_configured_nodes();
  EXPECT_EQ(Sched_affinity_manager::get_instance()->dynamic_unbind(initial_index_excessive), false);

  int initial_index = -1;
  EXPECT_EQ(Sched_affinity_manager::get_instance()->dynamic_bind(initial_index), true);
  EXPECT_EQ(initial_index, 0);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->dynamic_unbind(initial_index), true);

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

  default_config[sched_affinity::Thread_type::LOG_WRITER] = const_cast<char *>(std::string("0").c_str());
  default_config[sched_affinity::Thread_type::LOG_FLUSHER] = const_cast<char *>(std::string("1").c_str());
  default_config[sched_affinity::Thread_type::LOG_WRITE_NOTIFIER] = const_cast<char *>(std::string("2").c_str());
  default_config[sched_affinity::Thread_type::LOG_FLUSH_NOTIFIER] = const_cast<char *>(std::string("3").c_str());
  default_config[sched_affinity::Thread_type::LOG_CLOSER] = const_cast<char *>(std::string("4").c_str());
  default_config[sched_affinity::Thread_type::LOG_CHECKPOINTER] = const_cast<char *>(std::string("5").c_str());
  default_config[sched_affinity::Thread_type::PURGE_COORDINATOR] = const_cast<char *>(std::string("6").c_str());

  instance = Sched_affinity_manager::create_instance(default_config);
  ASSERT_NE(instance, nullptr);

  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::FOREGROUND), false);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_WRITER), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_FLUSHER), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_WRITE_NOTIFIER), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_FLUSH_NOTIFIER), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_CHECKPOINTER), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::LOG_CLOSER), true);
  EXPECT_EQ(Sched_affinity_manager::get_instance()->static_bind(sched_affinity::Thread_type::PURGE_COORDINATOR), true);

  Sched_affinity_manager::free_instance();
}

TEST_F(SchedAffinityManagerTest, TakeSnapshot) {
  if (skip_if_numa_unavailable()) {
    return;
  }

  std::string test_str = "0-" + std::to_string(test_process_cpu_num - 1);
  default_config[sched_affinity::Thread_type::FOREGROUND] = const_cast<char *>(test_str.c_str());
  auto instance = Sched_affinity_manager::create_instance(default_config);
  ASSERT_NE(instance, nullptr);

  /* test the case when the buff param is a null pointer */
  char *buff = nullptr;
  int buff_size = 1024;

  Sched_affinity_manager::get_instance()->take_snapshot(buff, buff_size + 1);
  EXPECT_EQ(buff, nullptr);

  int total_node_num = numa_num_configured_nodes();
  int total_cpu_num = numa_num_configured_cpus();
  int cpu_num_per_node = total_cpu_num / total_node_num;

  buff = (char *)malloc((1024 + 1) * sizeof(char));

  ASSERT_NE(buff, nullptr);

  int initial_group_index = -1;
  Sched_affinity_manager::get_instance()->dynamic_bind(initial_group_index);

  std::string criterion_str = "";
  std::string buffered_str = "";
  for (int i = 0; i < total_node_num; i++) {
    if (i == 0) {
      criterion_str += "1/" + std::to_string(cpu_num_per_node);
    } else {
      criterion_str += "0/" + std::to_string(cpu_num_per_node);
    }
    criterion_str += "; ";
  }

  /* test the case when the buff_size param is negative */
  buff_size = -2;
  Sched_affinity_manager::get_instance()->take_snapshot(buff, buff_size + 1);
  buffered_str = std::string(buff);
  EXPECT_NE(criterion_str, buffered_str);

  /* test the case when the params are both normal*/
  buff_size = 1024;
  Sched_affinity_manager::get_instance()->take_snapshot(buff, buff_size + 1);
  buffered_str = std::string(buff);
  EXPECT_EQ(criterion_str, buffered_str);

  free(buff);
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