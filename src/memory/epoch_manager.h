// Copyright (c) Microsoft Corporation. All rights reserved.
// Modified by Minhui Xie
// Licensed under the MIT license.
#pragma once
#include <deque>

#include "base/base.h"
#include "base/hash.h"
#include "base/log.h"

namespace base {

namespace epoch {

typedef uint64_t Epoch;

class MinEpochTable {
  struct Entry {
    static constexpr int kEpochListLength = 16;

    Entry() { epoch_list_.reset(new std::deque<Epoch>); }

    bool EnqueProtect(Epoch current_epoch) {
      epoch_list_->push_back(current_epoch);
      return true;
    }

    bool DequeUnProtect() {
      // CHECK(!epoch_list_->empty());
      epoch_list_->pop_front();
      return true;
    }

    Epoch MinEpoch() const {
      if (epoch_list_->size() == 0) return 0;
      return *std::min_element(epoch_list_->begin(), epoch_list_->end());
    }

    int PendingEpochNum() const { return epoch_list_->size(); }

    std::atomic<uint64_t> thread_id_{0};
    std::unique_ptr<std::deque<Epoch>> epoch_list_;
    char un_used[48];
  };
  static_assert(sizeof(Entry) == 64, "Unexpected table entry size");

 public:
  MinEpochTable(int max_thread_num = 64) : max_thread_num_(max_thread_num) {
    // (char[sizeof(Entry)]) "bla";
    table_ = new Entry[max_thread_num_];
    CHECK(table_);
  }

  ~MinEpochTable() { delete[] table_; }

  bool EnqueProtect(Epoch current_epoch) {
    Entry* entry = GetEntryForThread();
    return entry->EnqueProtect(current_epoch);
  }

  bool PopUnprotect() {
    Entry* entry = GetEntryForThread();
    entry->DequeUnProtect();
    return true;
  }

  Epoch ComputeNewSafeToReclaimEpoch(Epoch current_epoch) {
    Epoch oldest_call = current_epoch;
    for (uint64_t i = 0; i < max_thread_num_; ++i) {
      Entry& entry = table_[i];
      // If any other thread has flushed a protected epoch to the cache
      // hierarchy we're guaranteed to see it even with relaxed access.
      Epoch entryEpoch = entry.MinEpoch();
      if (entryEpoch != 0 && entryEpoch < oldest_call) {
        oldest_call = entryEpoch;
      }
    }
    // The latest safe epoch is the one just before the earlier unsafe one.
    return oldest_call - 1;
  }

  int MaxPendingEpochNumPerThread() const {
    int ret = 0;
    for (uint64_t i = 0; i < max_thread_num_; ++i) {
      Entry& entry = table_[i];
      ret = std::max(ret, entry.PendingEpochNum());
    }
    return ret;
  }

 private:
  Entry* GetEntryForThread() {
    thread_local Entry* thread_local_entry = nullptr;
    if (thread_local_entry) return thread_local_entry;
    uint64_t current_thread_id = pthread_self();
    thread_local_entry = ReserveEntry(
        GetHash(current_thread_id) % max_thread_num_, current_thread_id);
    return thread_local_entry;
  }

  Entry* ReserveEntry(uint64_t start_index, uint64_t thread_id) {
    auto start_ts = base::GetTimestamp();
    for (;;) {
      auto now_ts = base::GetTimestamp();
      if (now_ts - start_ts > 2 * 1e6) {
        LOG(FATAL) << "can not ReserveEntry";
      }
      // Reserve an entry in the table.
      for (uint64_t i = 0; i < max_thread_num_; ++i) {
        uint64_t indexToTest = (start_index + i) % max_thread_num_;
        Entry& entry = table_[indexToTest];
        if (entry.thread_id_ == 0) {
          uint64_t expected = 0;
          // Atomically grab a slot. No memory barriers needed.
          // Once the threadId is in place the slot is locked.
          bool success = entry.thread_id_.compare_exchange_strong(
              expected, thread_id, std::memory_order_relaxed);
          if (success) {
            return &table_[indexToTest];
          }
          // Ignore the CAS failure since the entry must be populated,
          // just move on to the next entry.
        }
      }
      ReclaimOldEntries();
    }
  }

 private:
  void ReclaimOldEntries() {}
  std::atomic<Epoch> current_epoch_;
  const int max_thread_num_;
  Entry* table_;
};

class EpochManager {
 private:
  EpochManager() : current_epoch_{1}, safe_to_reclaim_epoch_{0} {
    epoch_table_ = std::make_unique<MinEpochTable>();
  }

 public:
  static EpochManager* GetInstance() {
    static EpochManager instance;
    return &instance;
  }

  ~EpochManager(){};

  void Protect() {
    epoch_table_->EnqueProtect(current_epoch_.load(std::memory_order_relaxed));
  }

  void UnProtect() { epoch_table_->PopUnprotect(); }

  Epoch GetCurrentEpoch() {
    return current_epoch_.load(std::memory_order_seq_cst);
  }

  void BumpCurrentEpoch() {
    Epoch newEpoch = current_epoch_.fetch_add(1, std::memory_order_seq_cst);
    ComputeNewSafeToReclaimEpoch(newEpoch);
  }

  bool IsSafeToReclaim(Epoch epoch) {
    return epoch <= safe_to_reclaim_epoch_.load(std::memory_order_relaxed);
  }

  void ComputeNewSafeToReclaimEpoch(Epoch currentEpoch) {
    safe_to_reclaim_epoch_.store(
        epoch_table_->ComputeNewSafeToReclaimEpoch(currentEpoch),
        std::memory_order_release);
  }

  int MaxPendingEpochNumPerThread() const {
    return epoch_table_->MaxPendingEpochNumPerThread();
  }

 private:
  std::atomic<Epoch> current_epoch_;
  std::atomic<Epoch> safe_to_reclaim_epoch_;
  std::unique_ptr<MinEpochTable> epoch_table_;
  DISALLOW_COPY_AND_ASSIGN(EpochManager);
};

class IGarbageList {
 public:
  typedef void (*DestroyCallback)(void* callback_context, void* object);

  IGarbageList() {}

  virtual ~IGarbageList() {}

  virtual bool Initialize(EpochManager* epoch_manager,
                          size_t size = 4 * 1024 * 1024) {
    (epoch_manager);
    (size);
    return true;
  }

  virtual bool Uninitialize() { return true; }

  virtual bool Push(void* removed_item, DestroyCallback destroy_callback,
                    void* context) = 0;
};

}  // namespace epoch
}  // namespace base