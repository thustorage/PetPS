#include "DPUService.h"
#include "DSM.h"
#include "Directory.h"
#include "Global.h"
#include "Index.h"
#include "OpContext.h"
#include "SegmentAlloc.h"
#include "Timer.h"
#include "murmur_hash2.h"

#include <gperftools/profiler.h>

#include <iostream>
#include <unordered_map>

namespace kv {

DPUService::DPUService(DSM *dsm, uint32_t server_thread_nr)
    : dsm(dsm), server_thread_nr(server_thread_nr), is_run(false) {}

void DPUService::run() {

  Debug::notifyInfo("DPU Service start...");

  for (size_t i = 0; i < server_thread_nr; ++i) {
    kv_thread[i] = std::thread(&DPUService::run_thread, this, i);
  }

  // stat_thread = std::thread(&KVStore::stat, this);
}

// for per-worker stats
uint64_t dpu_counter[64][8];
void DPUService::stat() {
#ifdef ENABLE_PROFILE
  while (!is_run.load()) {
  }
  ProfilerStart("/home/wq_workspace/Mayfly/build/wq.profile");
  int k = 0;
  sleep(30);
  ProfilerStop();
  exit(-1);

#else
  return;
#endif
}

void DPUService::run_thread(int id) {

  auto_bind_core();
  dsm->registerThread();

  printf("I am worker %d\n", dsm->getMyThreadID());

  auto *ctx_manager = new OpContextManager(dsm);
  auto msg = RawMessage::get_new_msg();

  while (true) {
    msg->clear();

    Cursor cur;
    uint64_t wr_id;

    auto recv = dsm->rpc_fast_wait(&wr_id);

    msg->set_type(RpcType::RESP_GET_NOT_FOUND);
    dsm->rpc_call(msg, recv->node_id, recv->t_id);

    continue;

    dpu_counter[dsm->getMyThreadID()][0]++;

    assert(recv->type == RpcType::GET || recv->type == RpcType::PUT ||
           recv->type == RpcType::DEL);

    Slice k, v;
    CRCType kv_crc = recv->get<CRCType>(cur);
    KeySizeType k_size = recv->get<KeySizeType>(cur);
    ValueSizeType v_size;

    if (recv->type != RpcType::GET) { // put or del
      v_size = recv->get<ValueSizeType>(cur);
      k = recv->get_string_wo_size(cur, k_size);
      v = recv->get_string_wo_size(cur, v_size);
    } else {
      k = recv->get_string_wo_size(cur, k_size);
    }

    HashType hash;
    Index *index = nullptr;

    switch (recv->type) {
    case RpcType::GET: {

      recv->set_type(RpcType::GET); // change get_reroute => get

      auto e = index->get(k, hash);

      // std::cout << k.to_string() << std::endl;

      // printf("get ptr %p\n", e);

      if (e == nullptr) {
        msg->set_type(RpcType::RESP_GET_NOT_FOUND);
        dsm->rpc_call(msg, recv->node_id, recv->t_id);

      } else {
        msg->set_type(RpcType::RESP_GET_OK);
        dsm->rpc_call(msg, recv->node_id, recv->t_id, e->get_value());
      }

      break;
    }

    case RpcType::PUT: {
      msg->set_type(RpcType::RESP_PUT);
      index->put(k, nullptr, 1, hash);
      break;
    }

    case RpcType::DEL: {
      printf("DEL\n");

      break;
    }
    default:
      printf("unknown rpc op\n");
      break;
    }

    if (recv->type != RpcType::GET) {
      msg->rpc_tag = recv->rpc_tag;
      dsm->rpc_call(msg, recv->node_id, recv->t_id);
    }
  }
}

} // namespace kv
