#ifndef __CONNECTION_H__
#define __CONNECTION_H__

#include "Common.h"
#include "RawMessageConnection.h"

#include "DirectoryConnection.h"
#include "ThreadConnection.h"

struct RemoteConnection {
  // directory
  uint64_t dsmBase;

  uint32_t dsmRKey[NR_DIRECTORY];
  uint32_t dirMessageQPN[NR_DIRECTORY];
  ibv_ah *dirAh[NR_DIRECTORY];

  // cache
  uint64_t cacheBase;
  uint32_t cacheRKey[NR_DIRECTORY];

  // lock memory
  uint64_t lockBase;
  uint32_t lockRKey[NR_DIRECTORY];

  // app thread
  uint32_t appRKey[kv::kMaxNetThread];
  uint32_t appMessageQPN[kv::kMaxNetThread];
  ibv_ah *appAh[kv::kMaxNetThread];
};

#endif /* __CONNECTION_H__ */
