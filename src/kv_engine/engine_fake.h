#pragma once

#include "base/factory.h"
#include "base/timer.h"
#include "base_kv.h"

#define XMH_SIMPLE_MALLOC

DEFINE_int32(fake_kv_index_sleepns, 0, "sleep ns in the index part of FakeKV");

class KVEngineFakeKV : public BaseKV {
public:
  KVEngineFakeKV(const BaseKVConfig &config)
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
    LOG(FATAL) << "not implement";
  }
  void Put(const uint64_t key, const std::string_view &value,
           unsigned t) override {
    LOG(FATAL) << "not implement";
  }
  void BatchGet(base::ConstArray<uint64> keys,
                std::vector<base::ConstArray<float>> *values,
                unsigned t) override {
    xmh::Timer batch_get_timer("Fake KV");
    batch_get_timer.start();
    char *data_start = shm_malloc_.GetMallocData(0);
    for (auto k : keys) {
      base::Rdtsc::SleepNS(FLAGS_fake_kv_index_sleepns);
      int size = value_size_;
      char *data = data_start + k * size;
      values->emplace_back((float *)data, size / sizeof(float));
    }
    batch_get_timer.end();
  }

  std::pair<uint64_t, uint64_t> RegisterPMAddr() const override {
    return base::PMMmapRegisterCenter::GetInstance()->ForRDMAMemoryRegion();
  }

private:
  int value_size_;
#ifdef XMH_SIMPLE_MALLOC
  base::PersistSimpleMalloc shm_malloc_;
#else
  base::PersistLoopShmMalloc shm_malloc_;
#endif
  base::ShmFile valid_shm_file_; // 标记 shm 数据是否合法
};

FACTORY_REGISTER(BaseKV, KVEngineFakeKV, KVEngineFakeKV, const BaseKVConfig &);