#pragma once
#include "base/hash.h"
#include "Postoffice.h"

class ShardManager {
public:
  static int KeyPartition(uint64_t key) {
    return GetHash(key) % XPostoffice::GetInstance()->NumServers();
  }
};