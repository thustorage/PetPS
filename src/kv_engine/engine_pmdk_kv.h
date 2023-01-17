#pragma once

#include "base/factory.h"

#include "base_kv.h"

#include <libpmemkv.hpp>

class KVEnginePMKV : public BaseKV {
 public:
  KVEnginePMKV(const BaseKVConfig &config) : BaseKV(config) {
    pmem::kv::config cfg;
    pmem::kv::status s = cfg.put_path("/media/aep0/test");
    CHECK(s == pmem::kv::status::OK) << s;
    s = cfg.put_size(16 * 1024 * 1024 * 1024LL);
    CHECK(s == pmem::kv::status::OK);
    s = cfg.put_create_or_error_if_exists(true);
    CHECK(s == pmem::kv::status::OK);
    kv_.reset(new pmem::kv::db());
    CHECK(kv_.get() != nullptr);
    s = kv_->open("cmap", std::move(cfg));
    CHECK(s == pmem::kv::status::OK) << s;

    for (uint64_t i = 0; i < 1000; ++i) {
      kv_->put(std::string_view((char *)&i, sizeof(uint64_t)), "xxxx");
    }
    printf("OK\n");
  }
  void Get(const uint64_t key, std::string &value) override {
    kv_->get(std::string_view((char *)&key, sizeof(uint64_t)), &value);
  }
  void Put(const uint64_t key, const std::string_view &value) override {
    kv_->put(std::string_view((char *)&key, sizeof(uint64_t)), value);
  }
  void BatchGet(base::ConstArray<uint64> keys,
                std::vector<base::ConstArray<float>> *values) override {
    LOG(FATAL) << "todo";
  }

  std::pair<uint64_t, uint64_t> RegisterPMAddr() const override {
    return std::make_pair(0, 0);
  }

 private:
  std::unique_ptr<pmem::kv::db> kv_;
};

FACTORY_REGISTER(BaseKV, KVEnginePMKV, KVEnginePMKV, const BaseKVConfig &);