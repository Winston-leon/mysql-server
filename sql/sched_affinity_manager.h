#ifndef SCHED_AFFINITY_MANAGER_H
#define SCHED_AFFINITY_MANAGER_H
#include "my_config.h"
#ifdef HAVE_LIBNUMA
#include <numa.h>

#include <string>
#include <vector>

#include "mysql/psi/mysql_mutex.h"

class THD;

namespace sched_affinity {

enum Sched_affinity_thread_type {
  TT_FOREGROUND = 0,
  TT_LOG_WRITER,
  TT_LOG_FLUSHER,
  TT_LOG_WRITE_NOTIFIER,
  TT_LOG_FLUSH_NOTIFIER,
  TT_LOG_CLOSER,
  TT_LOG_CHECKPOINTER,
  TT_PURGE_COORDINATOR,
  TT_MAX
};

struct Sched_affinity_group {
  bitmask *avail_cpu_mask;
  int avail_cpu_num;
  int assigned_thread_num;
};

struct Sched_affinity_info {
  int total_cpu_num;
  int total_node_num;
  int cpu_num_per_node;
  bitmask *proc_avail_cpu_mask;
  bitmask *thread_bitmask[TT_MAX];
  bool enabled[TT_MAX];
};

class Sched_affinity_manager {
 public:
  static Sched_affinity_manager *create_instance(
      char *sched_affinity_args[TT_MAX]);
  static Sched_affinity_manager *get_instance();
  static void free_instance();
  Sched_affinity_manager(const Sched_affinity_manager &) = delete;
  Sched_affinity_manager &operator=(const Sched_affinity_manager &) = delete;
  Sched_affinity_manager(const Sched_affinity_manager &&) = delete;
  Sched_affinity_manager &operator=(const Sched_affinity_manager &&) = delete;

  bool init(char *sched_affinity_args[TT_MAX]);
  bool dynamic_bind(THD *);
  bool dynamic_unbind(THD *);
  bool static_bind(const Sched_affinity_thread_type);
  void take_snapshot(char *buff, int buff_size);
  const Sched_affinity_info& get_sched_affinity_info() const;

 private:
  Sched_affinity_manager();
  ~Sched_affinity_manager();
  bool init_sched_affinity_info(char *sched_affinity_args[TT_MAX]);
  void init_sched_affinity_group();
  bool check_foreground_background_conflict(bitmask *bm_foreground,
                                            bitmask *bm_background);
  bool check_thread_process_conflict(bitmask *bm_thread, bitmask *bm_proc);

 private:
  std::vector<Sched_affinity_group> m_sched_affinity_group;
  Sched_affinity_info m_sched_affinity_info;
  mysql_mutex_t m_mutex;
};
} // namespace sched_affinity
#endif /* HAVE_LIBNUMA */
#endif /* SCHED_AFFINITY_MANAGER_H */
