#pragma once
#include <memory>

#include "base_kv.h"
#include "base/factory.h"
#include "pet_kv/pet_kv.h"

DECLARE_int32(prefetch_method);

class KVEnginePetKV : public BaseKV {
 public:
  explicit KVEnginePetKV(const BaseKVConfig &config) : BaseKV(config) {
    std::string shm_path = config.path;
    const int shard_num = 16;
    shm_kv = std::make_unique<base::PetMultiKV>(
        shm_path + "/shm", shard_num,
        config.value_size * config.capacity / shard_num,
        // config.capacity / shard_num, config.value_size));
        config.capacity / shard_num, 0);
  }

  void Get(const uint64_t key, std::string &value, unsigned t) override {
    auto kv_data = shm_kv->Get(key);
    if (kv_data.data) value = std::string(kv_data.data, kv_data.size);
  }

  void BatchGet(base::ConstArray<uint64> keys,
                std::vector<base::ConstArray<float>> *values,
                unsigned t) override {
    values->clear();
    if (FLAGS_prefetch_method == 0) {
      for (auto k : keys) {
        auto kv_data = shm_kv->Get(k);
#ifdef RPC_DEBUG
        CHECK_NE(kv_data.size, 0) << "empty kv, key is " << k;
#endif
        values->emplace_back((float *)kv_data.data,
                             kv_data.size / sizeof(float));
      }
    } else if (FLAGS_prefetch_method == 1) {
      shm_kv->BatchGet(keys, values);
    }
  }

  void Put(const uint64_t key, const std::string_view &value,
           unsigned t) override {
    CHECK(shm_kv->Update(key, value.data(), value.size()));
  }

  std::pair<uint64_t, uint64_t> RegisterPMAddr() const override {
    return base::PMMmapRegisterCenter::GetInstance()->ForRDMAMemoryRegion();
  }

  void DebugInfo() const override { shm_kv->GetInfo(); }

 private:
  base::ScopedTempDir dir;
  std::unique_ptr<base::PetMultiKV> shm_kv;
};

FACTORY_REGISTER(BaseKV, KVEnginePetKV, KVEnginePetKV, const BaseKVConfig &);