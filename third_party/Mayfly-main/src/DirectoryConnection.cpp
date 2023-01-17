#include "DirectoryConnection.h"

#include "Connection.h"

DirectoryConnection::DirectoryConnection(uint16_t dirID, void *dsmPool,
                                         uint64_t dsmSize, uint32_t machineNR,
                                         RemoteConnection *remoteInfo,
                                         void *cachePool, uint64_t cacheSize,
                                         bool is_server)
    : dirID(dirID), remoteInfo(remoteInfo) {

  createContext(&ctx);
  cq = ibv_create_cq(ctx.ctx, RPC_QUEUE_SIZE, NULL, NULL, 0);
  if (cq == nullptr) {
    printf("error create cq 2\n");
  }

  message = new RawMessageConnection(ctx, cq, DIR_MESSAGE_NR);

  message->initRecv();
  message->initSend();

  // dsm memory
  this->dsmPool = dsmPool;
  this->dsmSize = dsmSize;
  this->dsmMR = createMemoryRegion((uint64_t)dsmPool, dsmSize, &ctx, is_server);
  this->dsmLKey = dsmMR->lkey;

  // cache memory
  this->cachePool = cachePool;
  this->cacheSize = cacheSize;
  this->cacheMR = createMemoryRegion((uint64_t)cachePool, cacheSize, &ctx);
  this->cacheLKey = cacheMR->lkey;

  // on-chip lock memory
  this->lockPool = (void *)define::kLockStartAddr;
  this->lockSize = define::kLockChipMemSize;
  // this->lockMR =
  //     createMemoryRegionOnChip((uint64_t)this->lockPool, this->lockSize,
  //     &ctx);
  // this->lockLKey = lockMR->lkey;

  for (int i = 0; i < kv::kMaxNetThread; ++i) {
    data2app[i] = new ibv_qp *[machineNR];
    for (size_t k = 0; k < machineNR; ++k) {
      createQueuePair(&data2app[i][k], IBV_QPT_RC, cq, &ctx);
    }
  }
}

void DirectoryConnection::sendMessage2App(RawMessage *m, uint16_t node_id,
                                          uint16_t th_id) {
  message->sendRawMessage(m, remoteInfo[node_id].appMessageQPN[th_id],
                          remoteInfo[node_id].appAh[th_id]);
  ;
}
