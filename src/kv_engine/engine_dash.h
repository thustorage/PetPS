#pragma once

#include "base/factory.h"
#include "base_kv.h"

#include "third_party/dash/src/Hash.h"
#include "third_party/dash/src/allocator.h"
#include "third_party/dash/src/ex_finger.h"

static const int valid_file_size = 123;

#define XMH_VARIABLE_SIZE_KV

class KVEngineDash : public BaseKV {
public:
  KVEngineDash(const BaseKVConfig &config)
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
    // 1 init dict
    base::file_util::CreateDirectory(config.path);
    dict_pool_name_ = config.path + "/dict";
    dict_pool_size_ =
        2 * base::PetHash<uint64, base::PetKVData, false>::MemorySize(config.capacity, true);

    LOG(ERROR) << "WARNING, we set 2x more memory for Dash";

    // 1.1: create (if not exist) and open the pool
    bool file_exist = false;
    if (FileExists(dict_pool_name_.c_str()))
      file_exist = true;
    Allocator::Initialize(dict_pool_name_.c_str(), dict_pool_size_);
    hash_table_ = reinterpret_cast<Hash<uint64_t> *>(
        Allocator::GetRoot(sizeof(extendible::Finger_EH<uint64_t>)));
    // 1.2: Initialize the hash table
    if (!file_exist) {
      // During initialization phase, allocate 64 segments for Dash-EH
      size_t segment_number = 64;
      new (hash_table_) extendible::Finger_EH<uint64_t>(
          segment_number, Allocator::Get()->pm_pool_);
    } else {
      new (hash_table_) extendible::Finger_EH<uint64_t>();
    }

    // 2 init value
    uint64_t value_shm_size = config.capacity * config.value_size;

    if (!valid_shm_file_.Initialize(config.path + "/valid", valid_file_size)) {
      base::file_util::Delete(config.path + "/valid", false);
      CHECK(
          valid_shm_file_.Initialize(config.path + "/valid", valid_file_size));
      shm_malloc_.Initialize();
    }

    LOG(INFO) << "After init: [shm_malloc] " << shm_malloc_.GetInfo();
  }

  void Get(const uint64_t key, std::string &value, unsigned t) override {
    Value_t read_value;
    base::PetKVData shmkv_data;
    auto epoch_guard = Allocator::AquireEpochGuard();
    if (hash_table_->Get(key, &read_value, true) == false) {
      // not found
      value = std::string();
    } else {
      shmkv_data = *(base::PetKVData *)(&read_value);
      char *data = shm_malloc_.GetMallocData(shmkv_data.shm_malloc_offset());
#ifdef XMH_VARIABLE_SIZE_KV
      int size = shm_malloc_.GetMallocSize(shmkv_data.shm_malloc_offset());
#else
      // TODO: warning, for remove a PM read
      int size = value_size_;
#endif
      value = std::string(data, size);
    }
  }
  void Put(const uint64_t key, const std::string_view &value,
           unsigned t) override {
    auto epoch_guard = Allocator::AquireEpochGuard();
    base::PetKVData shmkv_data;
    char *sync_data = shm_malloc_.New(value.size());
    shmkv_data.SetShmMallocOffset(shm_malloc_.GetMallocOffset(sync_data));
    memcpy(sync_data, value.data(), value.size());
    // warning(xieminhui) Now we only clflush the !!!!!value_len!!!!!
    // we may need to flush the whole value
    auto ret = hash_table_->Insert(key, (char *)shmkv_data.data_value, true);
    // CHECK(ret != -1);
  }
  void BatchGet(base::ConstArray<uint64> keys,
                std::vector<base::ConstArray<float>> *values,
                unsigned t) override {
    values->clear();
    for (auto k : keys) {
      auto epoch_guard = Allocator::AquireEpochGuard();
      Value_t read_value;
      base::PetKVData shmkv_data;
      if (hash_table_->Get(k, &read_value, true) == false) {
        values->emplace_back(nullptr, 0);
      } else {
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

  void Util() override { hash_table_->getNumber(); }

private:
  Hash<uint64_t> *hash_table_;
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

FACTORY_REGISTER(BaseKV, KVEngineDash, KVEngineDash, const BaseKVConfig &);