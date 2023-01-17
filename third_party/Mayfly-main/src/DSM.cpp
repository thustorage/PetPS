
#include "DSM.h"
#include "Directory.h"
#include "HugePageAlloc.h"

#include "DSMKeeper.h"

#include <algorithm>
#include <iostream>

thread_local int DSM::thread_id = -1;
thread_local ThreadConnection *DSM::iCon = nullptr;
// thread_local ThreadConnection *DSM::iCon2 = nullptr;
thread_local char *DSM::rdma_buffer = nullptr;
thread_local LocalAllocator DSM::local_allocator;

DSM *DSM::getInstance(const DSMConfig &conf, int globalID_xmh) {
  static DSM *dsm = nullptr;
  static WRLock lock;

  lock.wLock();
  if (!dsm) {
    dsm = new DSM(conf, globalID_xmh);
  } else {
  }
  lock.wUnlock();
  return dsm;
}

DSM::DSM(const DSMConfig &conf, int globalID_xmh)
    : conf(conf), appID(0), cache(conf.cacheConfig) {
  Debug::notifyInfo("Machine NR: %d [server: %d, client: %d]\n", conf.machineNR,
                    conf.cluster_info.serverNR, conf.cluster_info.clientNR);

  remoteInfo = new RemoteConnection[conf.machineNR];
  keeper =
      new DSMKeeper(thCon, dirCon, remoteInfo, globalID_xmh, conf.machineNR);
  myNodeID = keeper->getMyNodeID();

  if (conf.is_client && myNodeID < conf.cluster_info.serverNR) {
    printf("I am client but has error node id %d %d\n", myNodeID,
           conf.cluster_info.serverNR);
    exit(-1);
  }

  if (!is_server()) {
    if (0 == this->conf.baseAddr) {
      this->conf.dsmSize = 100 * define::MB;  // FIXME
      this->conf.baseAddr = (uint64_t)hugePageAlloc(this->conf.dsmSize);
    }
  } else {
    // pass
  }

  Debug::notifyInfo("shared memory size: %ldB, 0x%lx", this->conf.dsmSize,
                    this->conf.baseAddr);
  Debug::notifyInfo("cache size: %dMB", this->conf.cacheConfig.cacheSize);

  // std::cerr << "initRDMAConnection" << std::endl;
  initRDMAConnection(conf.NIC_name);
  // std::cerr << "initRDMAConnection done" << std::endl;

  if (!conf.is_client) {
    for (int i = 0; i < NR_DIRECTORY; ++i) {
      dirAgent[i] = new Directory(dirCon[i], remoteInfo, this, i, myNodeID);
      printf("dsm has been initialized\n");
    }
  }

  keeper->barrier("DSM-init", conf.machineNR);
}

DSM::~DSM() {}

void DSM::registerThread() {
  if (thread_id != -1) {
    return;
  }

  thread_id = appID.fetch_add(1);
  iCon = thCon[thread_id];
  iCon->message->initRecv();
  iCon->message->initSend();

  // iCon2 = thCon[kv::kMaxNetThread - 1 - thread_id];
  // assert(2 * thread_id + 1 <= kv::kMaxNetThread);

  if (!conf.is_client) {
    iCon->set_global_memory_lkey((void *)this->conf.baseAddr, conf.dsmSize);
    // iCon2->set_global_memory_lkey((void *)this->conf.baseAddr, conf.dsmSize);
  }

  rdma_buffer =
      ((char *)cache.data + 0 * define::MB) + thread_id * 1 * define::MB;
}

extern char rnic_name;
extern int gid_index;
void DSM::initRDMAConnection(char NIC_name) {
  rnic_name = NIC_name;
  gid_index = global_node_config[myNodeID].gid_index;

  for (int i = 0; i < kv::kMaxNetThread; ++i) {
    thCon[i] =
        new ThreadConnection(i, (void *)cache.data, cache.size * define::MB,
                             conf.machineNR, remoteInfo, false);
  }

  for (int i = 0; i < NR_DIRECTORY; ++i) {
    dirCon[i] =
        new DirectoryConnection(i, (void *)this->conf.baseAddr, conf.dsmSize,
                                conf.machineNR, remoteInfo, (void *)cache.data,
                                cache.size * define::MB, !conf.is_client);
  }
  std::cerr << std::flush;
  std::cout << std::flush;

  keeper->run();
}

void DSM::send(char *buffer, size_t size, uint16_t node_id, bool signal) {
  rdmaSend(iCon->data[0][node_id], (uint64_t)buffer, size, iCon->cacheLKey);
}

void DSM::send_sync(char *buffer, size_t size, uint16_t node_id) {
  send(buffer, size, node_id);
  ibv_wc wc;
  pollWithCQ(iCon->cq, 1, &wc);
}

void DSM::read_addr(const char *buffer, uint64_t addr, size_t size,
                    uint16_t node_id, uint64_t wr_id, bool signal) {
  rdmaRead(iCon->data[0][node_id], (uint64_t)buffer, addr, size,
           iCon->cacheLKey, remoteInfo[node_id].dsmRKey[0], signal, wr_id);
}

void DSM::read_addr_sync(const char *buffer, uint64_t addr, size_t size,
                         uint16_t node_id, uint64_t wr_id, bool signal) {
  read_addr(buffer, addr, size, node_id, wr_id, signal);

  ibv_wc wc;
  pollWithCQ(iCon->cq, 1, &wc);
}

void DSM::read(char *buffer, GlobalAddress gaddr, size_t size, bool signal) {
  rdmaRead(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
           remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, size,
           iCon->cacheLKey, remoteInfo[gaddr.nodeID].dsmRKey[0], signal);
}

void DSM::read_sync(char *buffer, GlobalAddress gaddr, size_t size) {
  read(buffer, gaddr, size);

  ibv_wc wc;
  pollWithCQ(iCon->cq, 1, &wc);
}

void DSM::write(const char *buffer, GlobalAddress gaddr, size_t size,
                bool signal, uint64_t wrID) {
  rdmaWrite(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
            remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, size,
            iCon->cacheLKey, remoteInfo[gaddr.nodeID].dsmRKey[0], -1, signal, wrID);
}

void DSM::write_sync(const char *buffer, GlobalAddress gaddr, size_t size) {
  write(buffer, gaddr, size);

  ibv_wc wc;
  pollWithCQ(iCon->cq, 1, &wc);
}

// buffer is in PM
void DSM::write_from_pm(const char *buffer, GlobalAddress gaddr, size_t size,
                        bool signal) {
  rdmaWrite(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
            remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, size,
            iCon->globalMemLkey, remoteInfo[gaddr.nodeID].dsmRKey[0], -1,
            signal);
}

void DSM::write_from_pm_sync(const char *buffer, GlobalAddress gaddr,
                             size_t size) {
  write_from_pm(buffer, gaddr, size);

  ibv_wc wc;
  pollWithCQ(iCon->cq, 1, &wc);
}

bool DSM::write_from_pm_vec(const SourceList *source_list, const int list_size,
                            GlobalAddress gaddr, bool signal, int sgePerWr, uint64_t wrID) {
  return rdmaWriteVector(
      iCon->data[0][gaddr.nodeID], source_list, list_size,
      remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, iCon->globalMemLkey,
      remoteInfo[gaddr.nodeID].dsmRKey[0], signal, wrID, sgePerWr);
}

void DSM::cas(GlobalAddress gaddr, uint64_t equal, uint64_t val,
              uint64_t *rdma_buffer, bool signal) {
  rdmaCompareAndSwap(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                     remoteInfo[gaddr.nodeID].dsmBase + gaddr.offset, equal,
                     val, iCon->cacheLKey, remoteInfo[gaddr.nodeID].dsmRKey[0],
                     signal);
}

bool DSM::cas_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                   uint64_t *rdma_buffer) {
  cas(gaddr, equal, val, rdma_buffer);
  ibv_wc wc;
  pollWithCQ(iCon->cq, 1, &wc);

  return equal == *rdma_buffer;
}

void DSM::read_dm(char *buffer, GlobalAddress gaddr, size_t size, bool signal) {
  rdmaRead(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
           remoteInfo[gaddr.nodeID].lockBase + gaddr.offset, size,
           iCon->cacheLKey, remoteInfo[gaddr.nodeID].lockRKey[0], signal);
}

void DSM::read_dm_sync(char *buffer, GlobalAddress gaddr, size_t size) {
  read_dm(buffer, gaddr, size);

  ibv_wc wc;
  pollWithCQ(iCon->cq, 1, &wc);
}

void DSM::write_dm(const char *buffer, GlobalAddress gaddr, size_t size,
                   bool signal) {
  rdmaWrite(iCon->data[0][gaddr.nodeID], (uint64_t)buffer,
            remoteInfo[gaddr.nodeID].lockBase + gaddr.offset, size,
            iCon->cacheLKey, remoteInfo[gaddr.nodeID].lockRKey[0], -1, signal);
}

void DSM::write_dm_sync(const char *buffer, GlobalAddress gaddr, size_t size) {
  write_dm(buffer, gaddr, size);

  ibv_wc wc;
  pollWithCQ(iCon->cq, 1, &wc);
}

void DSM::cas_dm(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                 uint64_t *rdma_buffer, bool signal) {
  rdmaCompareAndSwap(iCon->data[0][gaddr.nodeID], (uint64_t)rdma_buffer,
                     remoteInfo[gaddr.nodeID].lockBase + gaddr.offset, equal,
                     val, iCon->cacheLKey, remoteInfo[gaddr.nodeID].lockRKey[0],
                     signal);
}

bool DSM::cas_dm_sync(GlobalAddress gaddr, uint64_t equal, uint64_t val,
                      uint64_t *rdma_buffer) {
  cas_dm(gaddr, equal, val, rdma_buffer);
  ibv_wc wc;
  pollWithCQ(iCon->cq, 1, &wc);

  return equal == *rdma_buffer;
}

void DSM::poll_rdma_cq(int count) {
  ibv_wc wc;
  pollWithCQ(iCon->cq, count, &wc);
}