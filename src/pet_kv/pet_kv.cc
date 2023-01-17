#include "pet_kv.h"

#include <folly/GLog.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <utility>

#define FREE_SHM(ptr) shm_recycle_->Recycle(ptr);

namespace base {

constexpr bool IGNORE_LOAD_FACTOR = true;

PetKVData::Config PetKVData::kConfig = PetKVData::Config();

PetKV::PetKV(const std::string &shm_dir, int64 memory_size, int capacity,
             int pre_known_value_size)
    : start_ts_(base::GetTimestamp()),
      shm_dir_(shm_dir),
      pre_known_value_size_(pre_known_value_size) {
  ts_getter_ = &AsyncTimeHelper::GetTimestamp;
#if 1
  if (pre_known_value_size == 0)
    shm_malloc_ = new PersistMemoryPool<false>(
        shm_dir + "/value", memory_size,
        {8 + 32, 8 + 64, 8 + 128, 8 + 512, 8 + 1024});
  else
    shm_malloc_ = new PersistMemoryPool<true>(shm_dir + "/value", memory_size,
                                              {pre_known_value_size});
#else
  shm_malloc_ = new PersistLoopShmMalloc(shm_dir + "/value", memory_size);
#endif

  // shm_recycle_ = new DirectRecycle (shm_malloc_);
  // shm_recycle_ = new DelayedRecycle(shm_malloc_);
  shm_recycle_ = new ShmEpochRecycle(shm_malloc_);

  auto begin_ts = base::GetTimestamp() / 1000;
  LOG(INFO) << "PetKV " << shm_dir
            << " start initialize, pre_ms: " << (begin_ts - start_ts_ / 1000);

  uint64_t dict_size = capacity;
  auto dict_memory_size =
      ShmKDoubleDict::MemorySize(dict_size, IGNORE_LOAD_FACTOR);
  LOG(INFO) << fmt::format("PetKV allocate {:.2f} GB; capacity={} for dict",
                           (double)dict_memory_size / 1024 / 1024 / 1024,
                           dict_size);
  if (!dict_shm_file_.Initialize(shm_dir + "/dict", dict_memory_size)) {
    base::file_util::Delete(shm_dir + "/dict", false);
    CHECK(dict_shm_file_.Initialize(shm_dir + "/dict", dict_memory_size));
    LOG(INFO) << "Reinitialize shm dict size: " << dict_memory_size;
  }

  auto dict_file_init_ts = base::GetTimestamp() / 1000;

  if (!valid_shm_file_.Initialize(shm_dir + "/valid", valid_file_size)) {
    base::file_util::Delete(shm_dir + "/valid", false);
    CHECK(valid_shm_file_.Initialize(shm_dir + "/valid", valid_file_size));
    shm_malloc_->Initialize();
  }
  auto valid_file_init_ts = base::GetTimestamp() / 1000;

  dict_ = reinterpret_cast<ShmKDoubleDict *>(dict_shm_file_.Data());
  if (!dict_->Valid(dict_shm_file_.Size())) {
    dict_->Initialize(dict_size, IGNORE_LOAD_FACTOR);
  } else {
    LOG(INFO) << "Before recovery: [shm_malloc] " << shm_malloc_->GetInfo();
    dict_->Reload(dict_size, IGNORE_LOAD_FACTOR,
                  [&](uint64 key, PetKVData value) {
                    shm_malloc_->AddMallocs4Recovery(value.shm_malloc_offset());
                  });
    LOG(INFO) << "After recovery: [Dict] find " << dict_->Size() << " kvs";
    LOG(INFO) << "After recovery: [shm_malloc] " << shm_malloc_->GetInfo();
  }

  CHECK_EQ(dict_->Size(), shm_malloc_->total_malloc());
  auto all_valid_ts = base::GetTimestamp() / 1000;
  LOG(INFO) << "PetKV " << shm_dir << " initialize succeed, dict_file_init_ms: "
            << (dict_file_init_ts - begin_ts)
            << ", dict_init_ms: " << (valid_file_init_ts - dict_file_init_ts)
            << ", all_valid_ms: " << (all_valid_ts - valid_file_init_ts);
}

PetKV::~PetKV() {
  delete shm_recycle_;
  LOG(INFO) << "shm kv safe quit, remain dict size = " << dict_->Size();
  LOG(INFO) << shm_malloc_->GetInfo();
  delete shm_malloc_;
}

bool PetKV::Valid() {
  const int kCheckThreadNum = 3;
  auto begin_ts = base::GetTimestamp() / 1000;
  if (!dict_->Valid(dict_shm_file_.Size())) {
    LOG(ERROR) << "dict load error: " << dict_shm_file_.filename()
               << ", size: " << dict_shm_file_.Size();
    return false;
  }
  auto dict_check_ts = base::GetTimestamp() / 1000;
  auto check_ts = base::GetTimestamp() / 1000;
  auto shm_free_ts = base::GetTimestamp() / 1000;
  LOG(INFO) << "shm kv " << shm_dir_
            << " check valid, dict_check_ms: " << (dict_check_ts - begin_ts)
            << ", check_ms: " << (check_ts - dict_check_ts)
            << ", shm_free_ms: " << (shm_free_ts - check_ts);

  return true;
}

bool PetKV::Update(uint64 key, const char *log, int log_size) {
  base::AutoLock lock(modify_lock_);
  auto [p_cache, exists] = dict_->GetReturnPtr(key);

  char *value = shm_malloc_->New(log_size);
  if (nullptr == value) return false;
  memcpy(value, log, log_size);
  base::clflushopt_range(value, log_size);
  PetKVData new_cache(shm_malloc_->GetMallocOffset(value));

  if (!exists) {
    p_cache = dict_->Insert(key, new_cache, nullptr, true);
    if (p_cache == nullptr) {
      FB_LOG_EVERY_MS(WARNING, 2000) << "Update fail: " << key;
      FREE_SHM(shm_malloc_->GetMallocData(new_cache.shm_malloc_offset()));
      return false;
    }
    p_cache->SetShmMallocOffset(new_cache.shm_malloc_offset());
    p_cache->DoFlush();
  } else {
    FREE_SHM(shm_malloc_->GetMallocData(p_cache->shm_malloc_offset()));
    p_cache->SetShmMallocOffset(new_cache.shm_malloc_offset());
    p_cache->DoFlush();
  }

  // re-read the KV pair
  if (p_cache->shm_malloc_offset() != new_cache.shm_malloc_offset())
    FREE_SHM(shm_malloc_->GetMallocData(new_cache.shm_malloc_offset()));
  return true;
}

std::string PetKV::GetInfo() {
  std::string info;
  info.append(
      folly::sformat("cache: {}/{}\n", dict_->Size(), dict_->Capacity()));
  info.append(shm_malloc_->GetInfo());

  LOG(INFO) << folly::sformat("LoadFactor : {}/{}={}", dict_->Size(),
                              dict_->Capacity(),
                              dict_->Size() / (float)dict_->Capacity());
  LOG(INFO) << "MemoryUtil: "
            << dict_->Size() * 16 /
                   (float)ShmKDoubleDict::MemorySize((float)dict_->Capacity(),
                                                     IGNORE_LOAD_FACTOR);

  return info;
}

PetMultiKV::PetMultiKV(const std::vector<std::string> &shm_dir, int shard_num,
                       int64 shard_memory, int shard_cache_capacity,
                       int pre_known_value_size)
    : shm_dir_(shm_dir),
      shard_num_(shard_num),
      shard_memory_(shard_memory),
      shard_cache_capacity_(shard_cache_capacity),
      pre_known_value_size_(pre_known_value_size) {
  CHECK(!shm_dir_.empty());
  for (const auto &dir : shm_dir) {
    base::file_util::CreateDirectory(dir);
  }

  if (shard_memory_ >= (1LL << 35)) {
    for (int i = 36; i <= 64; ++i) {
      if ((1LL << i) > shard_memory_) {
        PetKVData::kConfig = PetKVData::Config(i - 3);
        LOG(INFO) << "shard_memory over 32G, change expire_time bit: "
                  << PetKVData::kConfig.kExpireBit
                  << ", shm_offset bit: " << (i - 3);
        break;
      }
    }
  }

  shm_kv_.resize(shard_num);
  std::vector<std::thread> thread_pool;
  for (int i = 0; i < shard_num; ++i) {
    thread_pool.emplace_back(&PetMultiKV::LoadShard, this, i);
  }
  for (int i = 0; i < shard_num; ++i) {
    thread_pool[i].join();
  }
}

void PetMultiKV::LoadShard(int shard) {
  LOG(INFO) << "PetMultiKV LoadShard shm_file:" << shm_dir(shard)
            << ", shard memory_size:" << shard_memory_
            << ", shard_cache_capacity:" << shard_cache_capacity_
            << ", pre_known_value_size:" << pre_known_value_size_;

  shm_kv_[shard] = new PetKV(shm_dir(shard), shard_memory_,
                             shard_cache_capacity_, pre_known_value_size_);
}
std::string PetMultiKV::GetInfo() {
  std::string info;
  for (int shard = 0; shard < shard_num_; ++shard) {
    info.append("shard " + base::IntToString(shard) + "\n");
    info.append(shm_kv_[shard]->GetInfo());
  }
  return info;
}

std::string PetMultiKV::shm_dir(int shard_id) {
  int idx = shard_id % shm_dir_.size();
  return shm_dir_[idx] + "/" + base::IntToString(shard_id);
}

}  // namespace base
