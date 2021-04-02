#ifndef SCHED_AFFINITY_MANAGER_H
#define SCHED_AFFINITY_MANAGER_H
#include "my_config.h"
#ifdef HAVE_LIBNUMA
#include <numa.h>
#endif

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mysql/psi/mysql_mutex.h"

namespace sched_affinity {
typedef long int PID_T;

enum class Thread_type {
  FOREGROUND,
  LOG_WRITER,
  LOG_FLUSHER,
  LOG_WRITE_NOTIFIER,
  LOG_FLUSH_NOTIFIER,
  LOG_CLOSER,
  LOG_CHECKPOINTER,
  PURGE_COORDINATOR
};

class Sched_affinity_manager {
 public:
  static Sched_affinity_manager *create_instance(
      const std::map<Thread_type, const char *> &);
  static Sched_affinity_manager *get_instance();
  static void free_instance();

  virtual bool bind_to_group(PID_T thread_id) = 0;
  virtual bool unbind_from_group(PID_T thread_id) = 0;
  virtual bool bind_to_target(const Thread_type &, PID_T thread_id) = 0;
  virtual bool reschedule(const std::map<Thread_type, const char *> &, const Thread_type &) = 0;
  virtual void take_snapshot(char *buff, int buff_size) = 0;
  virtual int get_total_node_number() = 0;
  virtual int get_cpu_number_per_node() = 0;

 protected:
  virtual bool init(const std::map<Thread_type, const char *> &) = 0;
  virtual ~Sched_affinity_manager() {}
};

class Sched_affinity_manager_dummy : public Sched_affinity_manager {
 public:
  Sched_affinity_manager_dummy(const Sched_affinity_manager_dummy &) = delete;
  Sched_affinity_manager_dummy &operator=(
      const Sched_affinity_manager_dummy &) = delete;
  Sched_affinity_manager_dummy(const Sched_affinity_manager_dummy &&) = delete;
  Sched_affinity_manager_dummy &operator=(
      const Sched_affinity_manager_dummy &&) = delete;

  bool bind_to_group(PID_T thread_id) override { return true; }
  bool unbind_from_group(PID_T thread_id) override { return true; }
  bool bind_to_target(const Thread_type &, PID_T thread_id) override { return true; }
  bool reschedule(const std::map<Thread_type, const char *> &, const Thread_type &) override {return true};
  void take_snapshot(char *buff, int buff_size) override;
  int get_total_node_number() override { return -1; }
  int get_cpu_number_per_node() override { return -1; }

 private:
  Sched_affinity_manager_dummy() : Sched_affinity_manager(){};
  ~Sched_affinity_manager_dummy(){};
  bool init(const std::map<Thread_type, const char *> &) override { return true; }
  friend class Sched_affinity_manager;
};

#ifdef HAVE_LIBNUMA

struct Sched_affinity_group {
  bitmask *avail_cpu_mask;
  int avail_cpu_num;
  int assigned_thread_num;
};

class Sched_affinity_manager_numa : public Sched_affinity_manager {
 public:
  Sched_affinity_manager_numa(const Sched_affinity_manager_numa &) = delete;
  Sched_affinity_manager_numa &operator=(const Sched_affinity_manager_numa &) =
      delete;
  Sched_affinity_manager_numa(const Sched_affinity_manager_numa &&) = delete;
  Sched_affinity_manager_numa &operator=(const Sched_affinity_manager_numa &&) =
      delete;

  bool bind_to_group(PID_T thread_id) override;
  bool unbind_from_group(PID_T thread_id) override;
  bool bind_to_target(const Thread_type &, PID_T thread_id) override;
  bool reschedule(const std::map<Thread_type, const char *> &, const Thread_type &) override;
  void take_snapshot(char *buff, int buff_size) override;
  int get_total_node_number() override;
  int get_cpu_number_per_node() override;

 private:
  Sched_affinity_manager_numa();
  ~Sched_affinity_manager_numa();
  bool init(const std::map<Thread_type, const char *> &) override;
  bool init_sched_affinity_info(const std::map<Thread_type, const char *> &);
  bool init_sched_affinity_group();
  bool check_foreground_background_compatibility(bitmask *bm_foreground,
                                                 bitmask *bm_background);
  bool check_thread_process_compatibility(bitmask *bm_thread, bitmask *bm_proc);
  friend class Sched_affinity_manager;

 private:
  std::vector<Sched_affinity_group> m_sched_affinity_group;
  int m_total_cpu_num;
  int m_total_node_num;
  int m_cpu_num_per_node;
  bitmask *m_process_bitmask;
  std::map<Thread_type, bitmask *> m_thread_bitmask;
  std::map<Thread_type, bool> m_thread_sched_enabled;
  std::vector<std::set<PID_T>> m_group_pid;
  std::vector<std::set<PID_T>> m_threadtype_pid
  mysql_mutex_t m_mutex;
};
#endif /* HAVE_LIBNUMA */
}  // namespace sched_affinity
#endif /* SCHED_AFFINITY_MANAGER_H */
