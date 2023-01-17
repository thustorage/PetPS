#ifndef __DSM_H__
#define __DSM_H__

#include <atomic>
#include <iostream>

#include "BatchMeta.h"
#include "Cache.h"
#include "Common.h"
#include "Config.h"
#include "Connection.h"
#include "DSMKeeper.h"
#include "GlobalAddress.h"
#include "LocalAllocator.h"
#include "SegmentAlloc.h"
#include "Timer.h"

class DSMKeeper;
class Directory;

class DSM {
 public:
  void registerThread();
  static DSM *getInstance(const DSMConfig &conf, int global_id = -1);

  uint16_t getMyNodeID() { return myNodeID; }
  uint16_t getMyThreadID() { return thread_id; }
  ThreadConnection *getThreadCon() { return iCon; }
  Directory *getDirAgent() { return dirAgent[0]; }

  bool is_server() { return myNodeID == kv::kServerNodeID; }

  bool is_client() { return !is_server(); }

  // RDMA operations
  // buffer is registered memory
  void send(char *buffer, size_t size, uint16_t node_id, bool signal = true);
  void send_sync(char *buffer, size_t size, uint16_t node_id);

  // buffer is on-chip memory
  void dm_persist_send(char *buffer, size_t size, uint16_t node_id,
                       uint64_t wr_id, bool signal = true);
  void dm_persist_send_sync(char *buffer, size_t size, uint16_t node_id,
                            uint64_t wr_id);

  // buffer is local cache
  void client_persist_send(char *buffer, size_t size, uint16_t node_id,
                           uint64_t wr_id, bool signal = true);
  void client_persist_send_sync(char *buffer, size_t size, uint16_t node_id,
                                uint64_t wr_id);

  // buffer is global memory
  void persist_send(char *buffer, size_t size, uint16_t node_id, uint64_t wr_id,
                    bool signal = true);
  void persist_send_sync(char *buffer, size_t size, uint16_t node_id,
                         uint64_t wr_id);

  void read_addr(const char *buffer, uint64_t addr, size_t size,
                 uint16_t node_id, uint64_t wr_id, bool signal = true);

  void read_addr_sync(const char *buffer, uint64_t addr, size_t size,
                      uint16_t node_id, uint64_t wr_id, bool signal = true);

  void read(char *buffer, GlobalAddress gaddr, size_t size, bool signal = true);
  void read_sync(char *buffer, GlobalAddress gaddr, size_t size);

  void write(const char *buffer, GlobalAddress gaddr, size_t size,
             bool signal = true, uint64_t wrID = -1);
  void write_sync(const char *buffer, GlobalAddress gaddr, size_t size);

  void write_from_pm(const char *buffer, GlobalAddress gaddr, size_t size,
                     bool signal = true);
  void write_from_pm_sync(const char *buffer, GlobalAddress gaddr, size_t size);

  bool write_from_pm_vec(const SourceList *source_list, const int list_size,
                         GlobalAddress gaddr, bool signal = true,
                         int sgePerWr = 30, uint64_t wrID = 0);

  void persist_write_addr(const char *buffer, uint64_t addr, size_t size,
                          uint16_t node_id, uint64_t wr_id, bool signal = true);

  void persist_write(const char *buffer, GlobalAddress gaddr, size_t size,
                     bool signal = true);
  void persist_write_sync(const char *buffer, GlobalAddress gaddr, size_t size);

  void cas(GlobalAddress gaddr, uint64_t equal, uint64_t val,
           uint64_t *rdma_buffer, bool signal = true);
  bool cas_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                uint64_t *rdma_buffer);

  void cas_mask(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                uint64_t *rdma_buffer, uint64_t mask = ~(0ull),
                bool signal = true);
  bool cas_mask_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                     uint64_t *rdma_buffer, uint64_t mask = ~(0ull));

  // for on-chip device memory
  void read_dm(char *buffer, GlobalAddress gaddr, size_t size,
               bool signal = true);
  void read_dm_sync(char *buffer, GlobalAddress gaddr, size_t size);

  void write_dm(const char *buffer, GlobalAddress gaddr, size_t size,
                bool signal = true);
  void write_dm_sync(const char *buffer, GlobalAddress gaddr, size_t size);

  void cas_dm(GlobalAddress gaddr, uint64_t equal, uint64_t val,
              uint64_t *rdma_buffer, bool signal = true);
  bool cas_dm_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                   uint64_t *rdma_buffer);

  void cas_dm_mask(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                   uint64_t *rdma_buffer, uint64_t mask = ~(0ull),
                   bool signal = true);
  bool cas_dm_mask_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                        uint64_t *rdma_buffer, uint64_t mask = ~(0ull));

  void poll_rdma_cq(int count = 1);

  // Memcached operations for sync
  size_t Put(uint64_t key, const void *value, size_t count) {
    std::string k = std::string("gam-") + std::to_string(key);
    keeper->memSet(k.c_str(), k.size(), (char *)value, count);
    return count;
  }

  size_t Get(uint64_t key, void *value) {
    std::string k = std::string("gam-") + std::to_string(key);
    size_t size;
    char *ret = keeper->memGet(k.c_str(), k.size(), &size);
    memcpy(value, ret, size);

    return size;
  }

  DSMConfig *get_conf() { return &conf; }

 private:
  DSM(const DSMConfig &conf, int global_id);
  ~DSM();

  void initRDMAConnection(char);

  DSMConfig conf;
  std::atomic_int appID;
  Cache cache;

  static thread_local int thread_id;
  static thread_local ThreadConnection *iCon;
  // static thread_local ThreadConnection *iCon2;
  static thread_local char *rdma_buffer;
  static thread_local LocalAllocator local_allocator;

  // uint64_t baseAddr;
  uint32_t myNodeID;

  RemoteConnection *remoteInfo;
  ThreadConnection *thCon[kv::kMaxNetThread];
  DirectoryConnection *dirCon[NR_DIRECTORY];
  DSMKeeper *keeper;

  Directory *dirAgent[NR_DIRECTORY];

 public:
  void barrier(const std::string &ss) { keeper->barrier(ss, conf.machineNR); }

  void barrier(const std::string &ss, int k) { keeper->barrier(ss, k); }

  char *get_dsm_base_addr() { return (char *)this->conf.baseAddr; }

  char *get_rdma_buffer() { return rdma_buffer; }

  GlobalAddress gaddr(void *addr) {
    GlobalAddress gaddr;
    gaddr.nodeID = getMyNodeID();
    gaddr.offset = (uint64_t)(addr) - this->conf.baseAddr;
    if (gaddr.offset >= this->conf.dsmSize) {
      std::cerr << "gaddr.offset=" << gaddr.offset << std::endl;
      std::cerr << "this->conf.dsmSize=" << this->conf.dsmSize << std::endl;
      assert(gaddr.offset < this->conf.dsmSize);
    }
    return gaddr;
  }

  char *addr(GlobalAddress gaddr) {
    assert(gaddr.nodeID == myNodeID);
    return (char *)(gaddr.offset + this->conf.baseAddr);
  }

  void rpc_call_dir(const RawMessage &m, uint16_t node_id,
                    uint16_t dir_id = 0) {
    auto buffer = (RawMessage *)iCon->message->getSendPool();

    memcpy(buffer, &m, m.size());
    buffer->node_id = myNodeID;
    buffer->t_id = thread_id;

    iCon->sendMessage2Dir(buffer, node_id, dir_id);
  }

  void rpc_call(RawMessage *m, uint16_t node_id, uint16_t t_id,
                Slice extra_data = Slice::Void(), bool is_server = true) {
    auto buffer = (RawMessage *)iCon->message->getSendPool();

    if (m->size() + 40 >= MESSAGE_SIZE) {
      Debug::notifyError("messeage size too large; exit -1");
      exit(-1);
    }
    memcpy(buffer, m, m->size());
    buffer->node_id = myNodeID;
    buffer->t_id = thread_id;

    if (extra_data.s) {  // reduce a memcpy
      buffer->add_string(extra_data);
      iCon->sendMessage(buffer, node_id, t_id);
    } else {
      iCon->sendMessage(buffer, node_id, t_id);
    }

    // buffer->print();
  }

  void rpc_call_wo_set_source(RawMessage *m, uint16_t node_id, uint16_t t_id) {
    auto buffer = (RawMessage *)iCon->message->getSendPool();

    assert(m->size() + 40 < MESSAGE_SIZE);

    memcpy(buffer, m, m->size());

    iCon->sendMessage(buffer, node_id, t_id);
  }

  RawMessage *rpc_fast_wait(uint64_t *wr_id = nullptr) {
    constexpr int kPollBatchSize = 16;
    thread_local ibv_wc wc[kPollBatchSize];
    thread_local int wc_size = 0;
    thread_local int wc_cur = 0;

    if (wc_cur >= wc_size) {
      wc_size = 0;
      while (wc_size <= 0) {
        wc_size = ibv_poll_cq(iCon->cq, kPollBatchSize, wc);
      }
      if (wc[0].status != IBV_WC_SUCCESS) {
        Debug::notifyError("Failed status %s (%d) for wr_id %d",
                           ibv_wc_status_str(wc[0].status), wc[0].status,
                           (int)wc[0].status);
        sleep(1);
      }
      wc_cur = 0;
      return rpc_fast_wait(wr_id);
    } else {
      if (wc[wc_cur].opcode == IBV_WC_RECV) {  // recv a message
        wc_cur++;
        return (RawMessage *)iCon->message->getMessage();
      }

      //      assert(wc[wc_cur].opcode == IBV_WC_RDMA_READ);
      *wr_id = wc[wc_cur].wr_id;

      wc_cur++;

      // prefetch
      if (wc_cur < wc_size && wc[wc_cur].opcode == IBV_WC_RECV) {
        __builtin_prefetch(iCon->message->getMessageAddr());
      }

      return nullptr;
    }
  }

  // RawMessage *rpc_fast_wait2(uint64_t *wr_id = nullptr) {

  //   constexpr int kPollBatchSize = 16;
  //   thread_local ibv_wc wc[kPollBatchSize];
  //   thread_local int wc_size = 0;
  //   thread_local int wc_cur = 0;

  //   if (wc_cur >= wc_size) {
  //     wc_size = 0;
  //     while (wc_size <= 0) {
  //       wc_size = ibv_poll_cq(iCon2->cq, kPollBatchSize, wc);
  //       if (wc_size <= 0)
  //         return nullptr;
  //     }
  //     if (wc[0].status != IBV_WC_SUCCESS) {
  //       Debug::notifyError("Failed status %s (%d) for wr_id %d",
  //                          ibv_wc_status_str(wc[0].status), wc[0].status,
  //                          (int)wc[0].status);
  //       sleep(1);
  //     }
  //     wc_cur = 0;
  //     return rpc_fast_wait2(wr_id);
  //   } else {
  //     if (wc[wc_cur].opcode == IBV_WC_RECV) { // recv a message
  //       wc_cur++;
  //       return (RawMessage *)iCon2->message->getMessage();
  //     }

  //     //      assert(wc[wc_cur].opcode == IBV_WC_RDMA_READ);
  //     *wr_id = wc[wc_cur].wr_id;

  //     wc_cur++;

  //     // prefetch
  //     if (wc_cur < wc_size && wc[wc_cur].opcode == IBV_WC_RECV) {
  //       __builtin_prefetch(iCon2->message->getMessageAddr());
  //     }

  //     return nullptr;
  //   }
  // }

  RawMessage *rpc_wait(uint64_t *wr_id = nullptr) {
    ibv_wc wc;

    pollWithCQ(iCon->cq, 1, &wc);

    if (wc.opcode == IBV_WC_RECV) {  // recv a message
      return (RawMessage *)iCon->message->getMessage();
    }

    // ack for persist ops
    // assert(wc.opcode == IBV_WC_RDMA_READ);
    *wr_id = wc.wr_id;
    return nullptr;
  }

  //
  RawMessage *rpc_wait_timeout(uint64_t deadline) {
    ibv_wc wc;

  retry:
    auto wc_size = ibv_poll_cq(iCon->cq, 1, &wc);
    if (wc_size <= 0) {
      if (Timer::get_time_ns() >= deadline) {
        return (RawMessage *)(1);
      }
      goto retry;
    }

    if (wc.status != IBV_WC_SUCCESS) {
      Debug::notifyError("Failed status %s (%d) for wr_id %d",
                         ibv_wc_status_str(wc.status), wc.status,
                         (int)wc.status);
      sleep(1);
    }

    if (wc.opcode == IBV_WC_RECV) {  // recv a message
      return (RawMessage *)iCon->message->getMessage();
    }

    return nullptr;
  }
};

#endif /* __DSM_H__ */
