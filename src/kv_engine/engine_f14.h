#pragma once

#include "base/factory.h"
#include "base_kv.h"

#include "pm_allocator.h"
#include "memory/persist_malloc.h"

#include <folly/container/F14Map.h>

#define XMH_VARIABLE_SIZE_KV
DECLARE_int32(prefetch_method);

class KVEngineF14 : public BaseKV {
  using TableType = folly::F14FastMap<
      uint64_t, uint64_t, std::hash<uint64_t>, std::equal_to<uint64_t>,
      pm_allocator::PMAllocator<std::pair<uint64_t const, uint64_t>>>;
  // using TableType =
  //     folly::F14FastMap<uint64_t, uint64_t, std::hash<uint64_t>,
  //                       std::equal_to<uint64_t>,
  //                       GenericAlloc<std::pair<uint64_t const, uint64_t>>>;
  // using TableType =
  //     folly::F14FastMap<uint64_t, uint64_t>;

public:
  KVEngineF14(const BaseKVConfig &config)
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
    auto dict_pool_name_ = config.path + "/dict";

    base::file_util::Delete(dict_pool_name_, false);
    bool file_exist = pm_allocator::FileExists(dict_pool_name_.c_str());
    pm_allocator::Allocator::Initialize(dict_pool_name_.c_str(),
                                        config.pool_size);

    hash_table_ = new TableType(config.capacity);

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

    hash_table_->insert({key, shmkv_data.data_value});
  }

  void BatchGet(base::ConstArray<uint64> keys,
                std::vector<base::ConstArray<float>> *values,
                unsigned tid) override {
    // TBD
    values->clear();
    if (FLAGS_prefetch_method == 0) {
      for (auto k : keys) {
        base::PetKVData shmkv_data;
        auto iter = hash_table_->find(k);
        if (iter == hash_table_->end()) {
          values->emplace_back(std::string());
        } else {
          uint64_t &read_value = iter->second;
          shmkv_data = *(base::PetKVData *)(&read_value);
          char *data =
              shm_malloc_.GetMallocData(shmkv_data.shm_malloc_offset());
#ifdef XMH_VARIABLE_SIZE_KV
          int size = shm_malloc_.GetMallocSize(shmkv_data.shm_malloc_offset());
#else
          // TODO: warning, for remove a PM read
          int size = value_size_;
#endif
          values->emplace_back((float *)data, size / sizeof(float));
        }
      }
    } else {
      for (int i = 0; i < keys.Size(); i++) {
        auto key = keys[i];
        if (UNLIKELY(i != keys.Size() - 1)) {
          auto prefetch_key = keys[i + 1];
          auto const token = hash_table_->prehash(prefetch_key);
          hash_table_->prefetch(token);
        }
        base::PetKVData shmkv_data;
        auto iter = hash_table_->find(key);
        if (iter == hash_table_->end()) {
          values->emplace_back(std::string());
        } else {
          uint64_t &read_value = iter->second;
          shmkv_data = *(base::PetKVData *)(&read_value);
          char *data =
              shm_malloc_.GetMallocData(shmkv_data.shm_malloc_offset());
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
  }

  std::pair<uint64_t, uint64_t> RegisterPMAddr() const override {
    return base::PMMmapRegisterCenter::GetInstance()->ForRDMAMemoryRegion();
  }

  ~KVEngineF14() {
    std::cout << "exit KVEngineF14" << std::endl;
    // hash_table_->hash_name();
  }

  void Util() override {
    LOG(INFO) << folly::sformat("LoadFactor: {}/{}={}", hash_table_->size(),
                                hash_table_->max_size(),
                                hash_table_->load_factor());

    LOG(INFO) << "MemoryUtil: "
              << hash_table_->size() * 16 /
                     (float)hash_table_->getAllocatedMemorySize();
  }

private:
  TableType *hash_table_;
  int value_size_;
#ifdef XMH_SIMPLE_MALLOC
  base::PersistSimpleMalloc shm_malloc_;
#else
  base::PersistLoopShmMalloc shm_malloc_;
#endif
  base::ShmFile valid_shm_file_; // 标记 shm 数据是否合法
};

FACTORY_REGISTER(BaseKV, KVEngineF14, KVEngineF14, const BaseKVConfig &);
