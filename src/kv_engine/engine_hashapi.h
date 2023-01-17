#pragma once

#include "base/factory.h"
#include "base_kv.h"

#include "memory/persist_malloc.h"

#include "library_loader.h"
// #include "third_party/HashEvaluation-for-petps/hash/common/hash_api.h"
// #include "third_party/HashEvaluation-for-petps/hash/PCLHT/clht_lb_res.h"

// using namespace PiBench;
#define XMH_VARIABLE_SIZE_KV

extern hash_api *create_hashtable_pclht(const hashtable_options_t &opt,
                                        unsigned sz, unsigned tnum);
extern hash_api *create_hashtable_level(const hashtable_options_t &opt,
                                        unsigned sz, unsigned tnum);
extern hash_api *create_hashtable_clevel(const hashtable_options_t &opt,
                                         unsigned sz, unsigned tnum);
extern hash_api *create_hashtable_cceh(const hashtable_options_t &opt,
                                       unsigned sz, unsigned tnum);
extern hash_api *create_hashtable_ccehvm(const hashtable_options_t &opt,
                                         unsigned sz, unsigned tnum);

static const int hash_api_valid_file_size = 123;

class HashAPI : public BaseKV {
public:
  HashAPI(const BaseKVConfig &config)
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
    auto r = base::file_util::CreateDirectory(config.path);
    dict_pool_name_ = config.path + "/dict";

    std::cout << dict_pool_name_ << std::endl;

    hashtable_options.pool_path = dict_pool_name_;
    hashtable_options.pool_size = config.pool_size;

    if (config.hash_name == "clht") {
      hash_table_ = create_hashtable_pclht(hashtable_options, config.hash_size,
                                           config.num_threads);
    } else if (config.hash_name == "level") {
      hash_table_ = create_hashtable_level(hashtable_options, config.hash_size,
                                           config.num_threads);
    } else if (config.hash_name == "clevel") {
      hash_table_ = create_hashtable_clevel(hashtable_options, config.hash_size,
                                            config.num_threads);
      // } else if (config.hash_name == "cceh") {
      //   hash_table_ = create_hashtable_cceh(hashtable_options,
      //   config.hash_size,
      //                                       config.num_threads);
    } else if (config.hash_name == "ccehvm") {
      auto r = base::file_util::CreateDirectory(dict_pool_name_);
      hash_table_ = create_hashtable_ccehvm(hashtable_options, config.hash_size,
                                            config.num_threads);
    } else {
      LOG(FATAL) << "invalid config.hash_name = " << config.hash_name;
    }

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
    uint64_t read_value;
    base::PetKVData shmkv_data;
    // auto epoch_guard = Allocator::AquireEpochGuard();
    if (hash_table_->find((char *)&key, sizeof(uint64_t), (char *)&read_value,
                          tid) == false) {
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
           unsigned tid) override {
    // auto epoch_guard = Allocator::AquireEpochGuard();
    base::PetKVData shmkv_data;
    char *sync_data = shm_malloc_.New(value.size());
    shmkv_data.SetShmMallocOffset(shm_malloc_.GetMallocOffset(sync_data));
    memcpy(sync_data, value.data(), value.size());
    // warning(xieminhui) Now we only clflush the !!!!!value_len!!!!!
    // we may need to flush the whole value
    auto ret = hash_table_->insert((char *)&key, sizeof(uint64_t),
                                   (char *)&shmkv_data.data_value,
                                   sizeof(uint64_t), tid, counter++);

    // uint64_t read_value;
    // auto rc = hash_table_->find((char *)&key, sizeof(uint64_t), (char
    // *)&read_value, tid);
    // if(rc == false) {
    //   std::cout << rc << std::endl;
    //   std::cout << read_value << "    ---    " << shmkv_data.data_value <<
    //   std::endl;
    // }

    // std::string c_str;
    // Get(key, c_str, 0);
    // CHECK_EQ(sync_data,
    //          shm_malloc_.GetMallocData(shmkv_data.shm_malloc_offset()));
    // CHECK_EQ(value, c_str) << "value read error";
    // if (value != c_str) {
    //   std::cout << value << " VS " << c_str << std::endl;
    // }
    // CHECK(value == c_str) << "key is" << key;

    // CHECK(ret != false);
  }

  void BatchGet(base::ConstArray<uint64> keys,
                std::vector<base::ConstArray<float>> *values,
                unsigned tid) override {
    // TBD
    values->clear();
    for (auto k : keys) {
      uint64_t read_value;
      base::PetKVData shmkv_data;
      // auto epoch_guard = Allocator::AquireEpochGuard();
      if (hash_table_->find((char *)&k, sizeof(uint64_t), (char *)&read_value,
                            tid) == false) {
        // printf("key: %d\n", (int)k);
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

  ~HashAPI() {
    std::cout << "exit HashAPI" << std::endl;
    hash_table_->hash_name();
  }

  void Util() { hash_table_->print_util(); }

private:
  hash_api *hash_table_;

  hashtable_options_t hashtable_options;
  uint64_t counter = 0; // NOTE(fyy) IDK what is this

  // debug
  uint64_t index_mem = 0;

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

FACTORY_REGISTER(BaseKV, HashAPI, HashAPI, const BaseKVConfig &);
