#if !defined(_HOST_SERVICE_H_)
#define _HOST_SERVICE_H_

#include "Common.h"
#include <cstdint>
#include <thread>

class DSM;

namespace kv {

class Index;

class HostService {

public:
  HostService(DSM *dsm, uint32_t server_thread_nr);

  void run();
  void stop();

private:
  DSM *dsm;
  Index *index;
  uint32_t server_thread_nr;
  std::atomic_bool is_run;
  std::thread kv_thread[kv::kMaxNetThread];
  std::thread stat_thread;

  void run_thread(int id);
  void stat();
};

} // namespace kv

#endif // _HOST_SERVICE_H_
