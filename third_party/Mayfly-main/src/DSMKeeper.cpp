#include "DSMKeeper.h"

#include "Connection.h"

const char *DSMKeeper::OK = "OK";
const char *DSMKeeper::ServerPrefix = "SPre";

void DSMKeeper::initLocalMeta() {
  localMeta.dsmBase = (uint64_t)dirCon[0]->dsmPool;
  localMeta.lockBase = (uint64_t)dirCon[0]->lockPool;
  localMeta.cacheBase = (uint64_t)thCon[0]->cachePool;

  localMeta.lid = thCon[0]->ctx.lid;
  memcpy((char *)localMeta.gid, (char *)(&thCon[0]->ctx.gid),
         16 * sizeof(uint8_t));

  // per thread APP
  for (int i = 0; i < kv::kMaxNetThread; ++i) {
    localMeta.appTh[i].rKey = thCon[i]->cacheMR->rkey;
    localMeta.appUdQpn[i] = thCon[i]->message->getQPN();
  }

  // per thread DIR
  for (int i = 0; i < NR_DIRECTORY; ++i) {
    localMeta.dirTh[i].rKey = dirCon[i]->dsmMR->rkey;
    // localMeta.dirTh[i].lock_rkey = dirCon[i]->lockMR->rkey;
    localMeta.dirTh[i].cacheRKey = dirCon[i]->cacheMR->rkey;

    localMeta.dirUdQpn[i] = dirCon[i]->message->getQPN();
  }
}

bool DSMKeeper::connectNode(uint16_t remoteID) {

  setDataToRemote(remoteID);

  std::string setK = setKey(remoteID);
  memSet(setK.c_str(), setK.size(), (char *)(&localMeta), sizeof(localMeta));

  std::string getK = getKey(remoteID);
  ExchangeMeta *remoteMeta = (ExchangeMeta *)memGet(getK.c_str(), getK.size());

  setDataFromRemote(remoteID, remoteMeta);

  free(remoteMeta);
  return true;
}

void DSMKeeper::setDataToRemote(uint16_t remoteID) {
  for (int i = 0; i < NR_DIRECTORY; ++i) {
    auto &c = dirCon[i];

    for (int k = 0; k < kv::kMaxNetThread; ++k) {
      localMeta.dirRcQpn2app[i][k] = c->data2app[k][remoteID]->qp_num;
    }
  }

  for (int i = 0; i < kv::kMaxNetThread; ++i) {
    auto &c = thCon[i];
    for (int k = 0; k < NR_DIRECTORY; ++k) {
      localMeta.appRcQpn2dir[i][k] = c->data[k][remoteID]->qp_num;
    }
  }
}

void DSMKeeper::setDataFromRemote(uint16_t remoteID, ExchangeMeta *remoteMeta) {
  for (int i = 0; i < NR_DIRECTORY; ++i) {
    auto &c = dirCon[i];

    for (int k = 0; k < kv::kMaxNetThread; ++k) {
      auto &qp = c->data2app[k][remoteID];

      assert(qp->qp_type == IBV_QPT_RC);
      modifyQPtoInit(qp, &c->ctx);
      modifyQPtoRTR(qp, remoteMeta->appRcQpn2dir[k][i], remoteMeta->lid,
                    remoteMeta->gid, &c->ctx);
      modifyQPtoRTS(qp);
    }
  }

  for (int i = 0; i < kv::kMaxNetThread; ++i) {
    auto &c = thCon[i];
    for (int k = 0; k < NR_DIRECTORY; ++k) {
      auto &qp = c->data[k][remoteID];

      assert(qp->qp_type == IBV_QPT_RC);
      modifyQPtoInit(qp, &c->ctx);
      modifyQPtoRTR(qp, remoteMeta->dirRcQpn2app[k][i], remoteMeta->lid,
                    remoteMeta->gid, &c->ctx);
      modifyQPtoRTS(qp);
    }
  }

  auto &info = remoteCon[remoteID];
  info.dsmBase = remoteMeta->dsmBase;
  info.cacheBase = remoteMeta->cacheBase;
  info.lockBase = remoteMeta->lockBase;

  for (int i = 0; i < NR_DIRECTORY; ++i) {
    info.dsmRKey[i] = remoteMeta->dirTh[i].rKey;
    info.lockRKey[i] = remoteMeta->dirTh[i].lock_rkey;
    info.cacheRKey[i] = remoteMeta->dirTh[i].cacheRKey;
    info.dirMessageQPN[i] = remoteMeta->dirUdQpn[i];

    struct ibv_ah_attr ahAttr;
    fillAhAttr(&ahAttr, remoteMeta->lid, remoteMeta->gid, &dirCon[i]->ctx);
    info.dirAh[i] = ibv_create_ah(dirCon[i]->ctx.pd, &ahAttr);
    assert(info.dirAh[i]);
  }

  for (int k = 0; k < kv::kMaxNetThread; ++k) {

    info.appRKey[k] = remoteMeta->appTh[k].rKey;
    info.appMessageQPN[k] = remoteMeta->appUdQpn[k];

    struct ibv_ah_attr ahAttr;
    fillAhAttr(&ahAttr, remoteMeta->lid, remoteMeta->gid, &thCon[k]->ctx);
    info.appAh[k] = ibv_create_ah(thCon[k]->ctx.pd, &ahAttr);
    assert(info.appAh[k]);
  }
}

void DSMKeeper::connectMySelf() {
  setDataToRemote(getMyNodeID());
  setDataFromRemote(getMyNodeID(), &localMeta);
}


void DSMKeeper::barrier(const std::string &barrierKey, uint64_t k) {

  std::string key = std::string("barrier-") + barrierKey;
  if (this->getMyNodeID() == this->getServerNR() - 1) {
    memSet(key.c_str(), key.size(), "0", 1);
  }
  memFetchAndAdd(key.c_str(), key.size());
  while (true) {
    uint64_t v = std::stoull(memGet(key.c_str(), key.size()));
    if (v == k) {
      return;
    }
  }
}

uint64_t DSMKeeper::sum(const std::string &sum_key, uint64_t value) {
  std::string key_prefix = std::string("sum-") + sum_key;

  std::string key = key_prefix + std::to_string(this->getMyNodeID());
  memSet(key.c_str(), key.size(), (char *)&value, sizeof(value));

  uint64_t ret = 0;
  for (int i = 0; i < this->getServerNR(); ++i) {
    key = key_prefix + std::to_string(i);
    ret += *(uint64_t *)memGet(key.c_str(), key.size());
  }

  return ret;
}
