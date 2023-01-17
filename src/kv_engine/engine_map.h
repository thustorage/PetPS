#pragma once

#include "base/factory.h"
#include "base_kv.h"

#include "memory/persist_malloc.h"

#include <shared_mutex>
#include <unordered_map>

#define XMH_VARIABLE_SIZE_KV

class KVEngineMap : public BaseKV {
public:
  KVEngineMap(const BaseKVConfig &config)
      : BaseKV(config),
#ifdef XMH_SIMPLE_MALLOC
        shm_malloc_(config.path + "/value", config.capacity * config.value_size,
                    config.value_size)
#else
        shm_malloc_(config.path + "/value",
                    1.2 * config.capacity * config.value_size)
#endif
  {
    value_size_ = config.value_size;

    // step1 init index pool
    // the map is stored in the dram, nothing to do.
    hash_table_ = new std::unordered_map<uint64_t, uint64_t>();

    // step2 init value
    uint64_t value_shm_size = config.capacity * config.value_size;

    if (!valid_shm_file_.Initialize(config.path + "/valid",
                                    hash_api_valid_file_size)) {
      base::file_util::Delete(config.path + "/valid", false);
      CHECK(valid_shm_file_.Initialize(config.path + "/valid",
                                       hash_api_valid_file_size));
      shm_malloc_.Initialize();
    }
    LOG(INFO) << "After init: [shm_malloc] " << shm_malloc_.GetInfo();
  }

  void Get(const uint64_t key, std::string &value, unsigned tid) override {

    base::PetKVData shmkv_data;
    std::shared_lock<std::shared_mutex> _(lock_);
    auto iter = hash_table_->find(key);

    if (iter == hash_table_->end()) {
      value = std::string();
    } else {
      uint64_t &read_value = iter->second;
      shmkv_data = *(base::PetKVData *)(&read_value);
      char *data = shm_malloc_.GetMallocData(shmkv_data.shm_malloc_offset());
#ifdef XMH_VARIABLE_SIZE_KV
      int size = shm_malloc_.GetMallocSize(shmkv_data.shm_malloc_offset());
#else
      int size = value_size_;
#endif
      value = std::string(data, size);
    }
  }

  void Put(const uint64_t key, const std::string_view &value,
           unsigned tid) override {

    base::PetKVData shmkv_data;
    char *sync_data = shm_malloc_.New(value.size());
    shmkv_data.SetShmMallocOffset(shm_malloc_.GetMallocOffset(sync_data));
    memcpy(sync_data, value.data(), value.size());

    std::unique_lock<std::shared_mutex> _(lock_);
    hash_table_->insert({key, shmkv_data.data_value});
  }

  void BatchGet(base::ConstArray<uint64> keys,
                std::vector<base::ConstArray<float>> *values,
                unsigned tid) override {
    // TBD
    values->clear();
    for (auto k : keys) {
      base::PetKVData shmkv_data;
      auto iter = hash_table_->find(k);

      if (iter == hash_table_->end()) {
        values->emplace_back(std::string());
      } else {
        uint64_t &read_value = iter->second;
        shmkv_data = *(base::PetKVData *)(&read_value);
        char *data = shm_malloc_.GetMallocData(shmkv_data.shm_malloc_offset());
#ifdef XMH_VARIABLE_SIZE_KV
        int size = shm_malloc_.GetMallocSize(shmkv_data.shm_malloc_offset());
#else
        // TODO: warning, for remove a PM read
        int size = value_size_;
#endif
        values->emplace_back((float *)data, size / sizeof(float));
      }
    }
  }

  std::pair<uint64_t, uint64_t> RegisterPMAddr() const override {
    return std::make_pair(0, 0);
  }

  ~KVEngineMap() {
    std::cout << "exit KVEngineMap" << std::endl;
    // hash_table_->hash_name();
  }

private:
  std::unordered_map<uint64_t, uint64_t> *hash_table_;
  std::shared_mutex lock_;

  hashtable_options_t hashtable_options;
  uint64_t counter = 0; // NOTE(fyy) IDK what is this

  std::string dict_pool_name_;
  size_t dict_pool_size_;
  int value_size_;
#ifdef XMH_SIMPLE_MALLOC
  base::PersistSimpleMalloc shm_malloc_;
#else
  base::PersistLoopShmMalloc shm_malloc_;
#endif
  base::ShmFile valid_shm_file_; // 标记 shm 数据是否合法
};

FACTORY_REGISTER(BaseKV, KVEngineMap, KVEngineMap, const BaseKVConfig &);
