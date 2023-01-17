#pragma once

#include "base/factory.h"
#include "base_kv.h"
#include "memory/persist_malloc.h"
#include "pm_allocator.h"

#include <shared_mutex>

// static const int hash_api_valid_file_size = 123;
#define XMH_VARIABLE_SIZE_KV

class KVEngineMapPM : public BaseKV {
  using TableType = std::unordered_map<
      uint64_t, uint64_t, std::hash<uint64_t>, std::equal_to<uint64_t>,
      pm_allocator::PMAllocator<std::pair<uint64_t, uint64_t>>>;

 public:
  KVEngineMapPM(const BaseKVConfig &config)
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
    dict_pool_name_ = config.path + "/dict";
    base::file_util::Delete(dict_pool_name_, false);

    bool file_exist = pm_allocator::FileExists(dict_pool_name_.c_str());
    pm_allocator::Allocator::Initialize(dict_pool_name_.c_str(),
                                        config.pool_size);

    hash_table_ = new TableType();

    // hash_table_ = reinterpret_cast<hash_api
    // *>(pm_allocator::Allocator::GetRoot(
    //         sizeof(pm)));

    // if(file_exist) {
    //   return new (hash_table_) pm();
    // }

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

  ~KVEngineMapPM() {
    std::cout << "exit KVEngineMapPM" << std::endl;
    pm_allocator::Allocator::PrintMem();
    printf("map size: %d\n", (int)hash_table_->size());
    // hash_table_->hash_name();
  }

 private:
  TableType *hash_table_;
  std::shared_mutex lock_;

  hashtable_options_t hashtable_options;
  uint64_t counter = 0;  // NOTE(fyy) IDK what is this

  std::string dict_pool_name_;
  size_t dict_pool_size_;
  int value_size_;
#ifdef XMH_SIMPLE_MALLOC
  base::PersistSimpleMalloc shm_malloc_;
#else
  base::PersistLoopShmMalloc shm_malloc_;
#endif
  base::ShmFile valid_shm_file_;  // 标记 shm 数据是否合法
};

FACTORY_REGISTER(BaseKV, KVEngineMapPM, KVEngineMapPM, const BaseKVConfig &);
