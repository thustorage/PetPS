#pragma once

#include <stdlib.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/array.h"
#include "base/async_time.h"
#include "base/base.h"
#include "base/hash.h"
#include "memory/persist_malloc.h"
#include "memory/shm_file.h"
#include "persistence.h"
#include "pet_hash.h"
#include "shm_common.h"

namespace base {

class PetKV {
  typedef PetHash<uint64, PetKVData, true> ShmKDoubleDict;
  static constexpr int valid_file_size = 4;
  inline static const std::string valid_file_tag = "valid tag";

 public:
  explicit PetKV(const std::string &shm_dir, int64 memory_size, int capacity,
                 int pre_known_value_size = 0);
  ~PetKV();

  bool Update(uint64 key, const char *log, int log_size);

  PetKVReadData Get(uint64 key) const {
    PetKVReadData read_data;
    auto [cache, exists] = dict_->Get(key);
    if (!exists) return read_data;
    read_data.expire_timet = cache.expire_timet();
    read_data.data = shm_malloc_->GetMallocData(cache.shm_malloc_offset());
    if (pre_known_value_size_ == 0) {
      read_data.size = shm_malloc_->GetMallocSize(cache.shm_malloc_offset());
    } else {
      read_data.size = pre_known_value_size_;
    }
    return read_data;
  }

  inline void HintPrefetch(const uint64 key) const { dict_->HintPrefetch(key); }

  bool Valid();

  std::string GetInfo();
  int64 key_num() const { return dict_->Size(); }

 private:
  uint64_t start_ts_ = 0;

  ShmKDoubleDict *dict_ = nullptr;
  MallocApi *shm_malloc_;
  ShmBaseRecycle *shm_recycle_;

  ShmFile dict_shm_file_;
  ShmFile valid_shm_file_;

  std::string shm_dir_;
  TimestampGetter ts_getter_ = nullptr;

  // 这个锁是保证写安全
  base::Lock modify_lock_;

  int pre_known_value_size_ = 0;

  DISALLOW_COPY_AND_ASSIGN(PetKV);
};

class PetMultiKV {
 public:
  explicit PetMultiKV(const std::vector<std::string> &shm_dir, int shard_num,
                      int64 shard_memory, int shard_cache_capacity,
                      int pre_known_value_size = 0);
  explicit PetMultiKV(const std::string &shm_dir, int shard_num,
                      int64 shard_memory, int shard_cache_capacity,
                      int pre_known_value_size = 0)
      : PetMultiKV(std::vector<std::string>{shm_dir}, shard_num, shard_memory,
                   shard_cache_capacity, pre_known_value_size) {}
  ~PetMultiKV() {
    for (auto shm_kv : shm_kv_) delete shm_kv;
  }
  int GetShard(uint64 key) const {
    return GetHashWithLevel(key, 1) % shard_num_;
  }

  bool Update(uint64 key, const char *log, int log_size) {
    return shm_kv_[GetShard(key)]->Update(key, log, log_size);
  }

  void BatchGet(base::ConstArray<uint64> keys,
                std::vector<base::ConstArray<float>> *values) {
    for (int i = 0; i < keys.Size(); i++) {
      auto key = keys[i];
      if (UNLIKELY(i != keys.Size() - 1)) {
        auto prefetch_key = keys[i + 1];
        shm_kv_[GetShard(prefetch_key)]->HintPrefetch(prefetch_key);
      }
      auto read_data = shm_kv_[GetShard(key)]->Get(key);
      CHECK_NE(read_data.size, 0);
      values->emplace_back((float *)read_data.data,
                           read_data.size / sizeof(float));
    }
  }

  PetKVReadData Get(uint64 key) const {
    return shm_kv_[GetShard(key)]->Get(key);
  }

  std::string GetInfo();
  
  int64 key_num() const {
    int64 key_num = 0;
    for (auto shm_kv : shm_kv_) key_num += shm_kv->key_num();
    return key_num;
  }

  int shard_num() const { return shard_num_; }
  const std::string &shm_dir() const { return shm_dir_.front(); }

 private:
  std::string shm_dir(int shard_id);
  void LoadShard(int shard);

  std::vector<std::string> shm_dir_;
  int shard_num_;
  int64 shard_memory_;
  int shard_cache_capacity_;
  std::vector<PetKV *> shm_kv_;
  int pre_known_value_size_ = 0;
  DISALLOW_COPY_AND_ASSIGN(PetMultiKV);
};

}  // namespace base
