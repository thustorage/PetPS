#ifndef __THREADCONNECTION_H__
#define __THREADCONNECTION_H__

#include "Common.h"
#include "RawMessageConnection.h"

struct RemoteConnection;

// app thread
struct ThreadConnection {

  uint16_t threadID;

  RdmaContext ctx;
  ibv_cq *cq;

  RawMessageConnection *message;

  ibv_qp **data[NR_DIRECTORY];

  ibv_mr *cacheMR;
  void *cachePool;
  uint32_t cacheLKey;

  uint32_t globalMemLkey;
  RemoteConnection *remoteInfo;

  uint32_t chipMemSize;
  uint32_t chipMemLkey;

  ThreadConnection(uint16_t threadID, void *cachePool, uint64_t cacheSize,
                   uint32_t machineNR, RemoteConnection *remoteInfo,
                   bool is_dpu = false);

  void sendMessage2Dir(RawMessage *m, uint16_t node_id, uint16_t dir_id = 0);
  void sendMessage(RawMessage *m, uint16_t node_id, uint16_t thread_id);
  void sendMessageWithData(RawMessage *m, Slice data, uint16_t node_id,
                           uint16_t thread_id);

  void set_global_memory_lkey(void *dsmPool, uint64_t dsmSize);
};

#endif /* __THREADCONNECTION_H__ */
