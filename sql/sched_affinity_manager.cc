#include "sql/sched_affinity_manager.h"

#ifdef HAVE_LIBNUMA
#include <cstdio>

#include "mysql/components/services/log_builtins.h"
#include "sql/sql_class.h"
#include "sql/mysqld.h"

namespace sched_affinity {

class Lock_guard {
 public:
  explicit Lock_guard(mysql_mutex_t &mutex) {
    m_mutex = &mutex;
    mysql_mutex_lock(m_mutex);
  }
  Lock_guard(const Lock_guard &) = delete;
  Lock_guard &operator=(const Lock_guard &) = delete;
  ~Lock_guard() { mysql_mutex_unlock(m_mutex); }

 private:
  mysql_mutex_t *m_mutex;
};

Sched_affinity_manager::Sched_affinity_manager() {
  mysql_mutex_init(key_sched_affinity_mutex, &m_mutex, nullptr);

  m_sched_affinity_info.total_cpu_num = 0;
  m_sched_affinity_info.total_node_num = 0;
  m_sched_affinity_info.cpu_num_per_node = 0;
  m_sched_affinity_info.proc_avail_cpu_mask = nullptr;
  for (int i = TT_FOREGROUND; i < TT_MAX; ++i) {
    m_sched_affinity_info.enabled[i] = false;
    m_sched_affinity_info.thread_bitmask[i] = nullptr;
  }
}

Sched_affinity_manager::~Sched_affinity_manager() {
  mysql_mutex_destroy(&m_mutex);

  if (m_sched_affinity_info.proc_avail_cpu_mask != nullptr) {
    numa_free_cpumask(m_sched_affinity_info.proc_avail_cpu_mask);
    m_sched_affinity_info.proc_avail_cpu_mask = nullptr;
  }
  for (int i = TT_FOREGROUND; i < TT_MAX; ++i) {
    if (m_sched_affinity_info.thread_bitmask[i] != nullptr) {
      numa_free_cpumask(m_sched_affinity_info.thread_bitmask[i]);
      m_sched_affinity_info.thread_bitmask[i] = nullptr;
    }
  }
  for (auto sched_affinity_group : m_sched_affinity_group) {
    if (sched_affinity_group.avail_cpu_mask != nullptr) {
      numa_free_cpumask(sched_affinity_group.avail_cpu_mask);
      sched_affinity_group.avail_cpu_mask = nullptr;
    }
  }
}

bool Sched_affinity_manager::init(char *sched_affinity_args[TT_MAX]) {
  if (!init_sched_affinity_info(sched_affinity_args)) {
    return false;
  }
  init_sched_affinity_group();
  return true;
}

bool Sched_affinity_manager::init_sched_affinity_info(
    char *sched_affinity_args[TT_MAX]) {
  m_sched_affinity_info.total_cpu_num = numa_num_configured_cpus();
  m_sched_affinity_info.total_node_num = numa_num_configured_nodes();
  m_sched_affinity_info.cpu_num_per_node = m_sched_affinity_info.total_cpu_num /
                                           m_sched_affinity_info.total_node_num;

  m_sched_affinity_info.proc_avail_cpu_mask = numa_allocate_cpumask();
  numa_sched_getaffinity(0, m_sched_affinity_info.proc_avail_cpu_mask);

  for (int i = TT_FOREGROUND; i < TT_MAX; ++i) {
    if (sched_affinity_args[i] == nullptr) {
      continue;
    } else if (!(m_sched_affinity_info.thread_bitmask[i] =
                     numa_parse_cpustring(sched_affinity_args[i]))) {
      LogErr(ERROR_LEVEL, ER_CANT_PARSE_CPU_STRING, sched_affinity_args[i]);
      return false;
    } else if (!check_thread_process_conflict(
                   m_sched_affinity_info.thread_bitmask[i],
                   m_sched_affinity_info.proc_avail_cpu_mask)) {
      LogErr(ERROR_LEVEL, ER_SCHED_AFFINITY_THREAD_PROCESS_CONFLICT);
      return false;
    }
    m_sched_affinity_info.enabled[i] = true;
  }

  if (m_sched_affinity_info.enabled[TT_FOREGROUND]) {
    for (int i = TT_FOREGROUND + 1; i < TT_MAX; ++i) {
      if (m_sched_affinity_info.enabled[i] &&
          !check_foreground_background_conflict(
              m_sched_affinity_info.thread_bitmask[TT_FOREGROUND],
              m_sched_affinity_info.thread_bitmask[i])) {
        LogErr(WARNING_LEVEL, ER_SCHED_AFFINITY_FOREGROUND_BACKGROUND_CONFLICT);
      }
    }
  }

  return true;
}

void Sched_affinity_manager::init_sched_affinity_group() {
  if (!m_sched_affinity_info.enabled[TT_FOREGROUND]) {
    return;
  }
  m_sched_affinity_group.resize(m_sched_affinity_info.total_node_num);

  for (int i = 0; i < m_sched_affinity_info.total_node_num; ++i) {
    m_sched_affinity_group[i].avail_cpu_num = 0;
    m_sched_affinity_group[i].avail_cpu_mask = numa_allocate_cpumask();

    for (int j = m_sched_affinity_info.cpu_num_per_node * i;
         j < m_sched_affinity_info.cpu_num_per_node * (i + 1); ++j) {
      if (numa_bitmask_isbitset(
              m_sched_affinity_info.thread_bitmask[TT_FOREGROUND], j)) {
        numa_bitmask_setbit(m_sched_affinity_group[i].avail_cpu_mask, j);
        ++m_sched_affinity_group[i].avail_cpu_num;
      }
    }

    m_sched_affinity_group[i].assigned_thread_num = 0;
  }
}

bool Sched_affinity_manager::check_foreground_background_conflict(
    bitmask *bm_foreground, bitmask *bm_background) {
  for (auto i = 0; i < m_sched_affinity_info.total_cpu_num; ++i) {
    if (numa_bitmask_isbitset(bm_foreground, i) &&
        numa_bitmask_isbitset(bm_background, i)) {
      return false;
    }
  }
  return true;
}

bool Sched_affinity_manager::check_thread_process_conflict(bitmask *bm_thread,
                                                           bitmask *bm_proc) {
  for (auto i = 0; i < m_sched_affinity_info.total_cpu_num; ++i) {
    if (numa_bitmask_isbitset(bm_thread, i) &&
        !numa_bitmask_isbitset(bm_proc, i)) {
      return false;
    }
  }
  return true;
}

bool Sched_affinity_manager::dynamic_bind(THD *thd) {
  if (!m_sched_affinity_info.enabled[TT_FOREGROUND]) {
    return false;
  }

  const Lock_guard lock(m_mutex);

  if (m_sched_affinity_group.empty()) {
    return false;
  }

  auto best_index = 0u;
  for (auto i = 1u; i < m_sched_affinity_group.size(); ++i) {
    if ((double)m_sched_affinity_group[i].assigned_thread_num /
            m_sched_affinity_group[i].avail_cpu_num <
        (double)m_sched_affinity_group[best_index].assigned_thread_num /
            m_sched_affinity_group[best_index].avail_cpu_num) {
      best_index = i;
    }
  }

  auto ret = numa_sched_setaffinity(
      0, m_sched_affinity_group[best_index].avail_cpu_mask);
  if (ret == 0) {
    ++m_sched_affinity_group[best_index].assigned_thread_num;
    thd->set_sched_affinity_group_index(best_index);
    return true;
  } else {
    return false;
  }
}

bool Sched_affinity_manager::dynamic_unbind(THD *thd) {
  if (!m_sched_affinity_info.enabled[TT_FOREGROUND]) {
    return false;
  }
  const Lock_guard lock(m_mutex);
  --m_sched_affinity_group[thd->get_sched_affinity_group_index()]
        .assigned_thread_num;
  return true;
}

bool Sched_affinity_manager::static_bind(
    const Sched_affinity_thread_type thread_type) {
  if (!m_sched_affinity_info.enabled[thread_type]) {
    return false;
  }
  auto ret = numa_sched_setaffinity(
      0, m_sched_affinity_info.thread_bitmask[thread_type]);
  return ret == 0 ? true : false;
}

void Sched_affinity_manager::take_snapshot(char *buff, int buff_size) {
  if (buff == nullptr || buff_size <= 0) {
    return;
  }
  const Lock_guard lock(m_mutex);
  int used_buff_size = 0;
  for (auto sched_affinity_group : m_sched_affinity_group) {
    int used = snprintf(buff + used_buff_size, buff_size - used_buff_size,
                        "%d/%d; ", sched_affinity_group.assigned_thread_num,
                        sched_affinity_group.avail_cpu_num);
    if (used > 0) {
      used_buff_size += used;
    }
    if (used_buff_size + 1 >= buff_size) {
      break;
    }
  }
}

const Sched_affinity_info &Sched_affinity_manager::get_sched_affinity_info()
    const {
  return m_sched_affinity_info;
}

static Sched_affinity_manager *sched_affinity_manager = nullptr;

Sched_affinity_manager *Sched_affinity_manager::create_instance(
    char *sched_affinity_args[TT_MAX]) {
  if (numa_available() == -1) {
    return nullptr;
  }
  Sched_affinity_manager::free_instance();
  sched_affinity_manager = new Sched_affinity_manager();
  if (!Sched_affinity_manager::get_instance()->init(sched_affinity_args)) {
    return nullptr;
  }
  return sched_affinity_manager;
}

Sched_affinity_manager *Sched_affinity_manager::get_instance() {
  return sched_affinity_manager;
}

void Sched_affinity_manager::free_instance() {
  if (sched_affinity_manager != nullptr) {
    delete sched_affinity_manager;
    sched_affinity_manager = nullptr;
  }
}
}  // namespace sched_affinity
#endif /* HAVE_LIBNUMA */
