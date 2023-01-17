#pragma once

#include <fcntl.h>
#include <sys/mman.h>

#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/async_time.h"
#include "base/bitmap.h"
#include "malloc.h"
#include "shm_file.h"
#include "pet_kv/shm_common.h"

namespace base {

class PersistSimpleMalloc : public MallocApi {
 public:
  PersistSimpleMalloc(const std::string &filename, int64 memory_size,
                      int slab_size)
      : memory_size_(memory_size), slab_size_(slab_size) {
    bool file_exists = base::file_util::PathExists(filename);

    meta_data_memory_size_ = memory_size_ / slab_size_ * sizeof(uint64_t);

    if (!shm_file_.Initialize(filename, memory_size + meta_data_memory_size_)) {
      file_exists = false;
      CHECK(base::file_util::Delete(filename, false));
      CHECK(
          shm_file_.Initialize(filename, memory_size + meta_data_memory_size_))
          << filename << " " << memory_size;
    }
    Initialize();
    meta_data_block_ = (uint64_t *)shm_file_.Data();
    data_ = shm_file_.Data() + meta_data_memory_size_;
    if (!file_exists) {
      LOG(INFO) << "PersistSimpleMalloc: first initialization.";
    } else {
      LOG(INFO) << "PersistSimpleMalloc: Recovery from shutdown.";
    }

    LOG(WARNING) << "skip the first allocate";
    allocated_.fetch_add(slab_size_);
  }

  char *New(int malloc_size) override {
    CHECK_EQ(malloc_size, slab_size_);
    auto offset = allocated_.fetch_add(malloc_size);
    nr_malloc_++;
    // TODO allocate
    if (offset + slab_size_ >= memory_size_) {
      offset = 0;
      allocated_ = 0;
    }
    CHECK_LE(offset + slab_size_, memory_size_);
    meta_data_block_[offset / slab_size_] = malloc_size;
    return data_ + offset;
  }

  bool Free(void *memory_data) override {
    LOG(FATAL) << "not implement";
    return false;
  }

  void AddMallocs4Recovery(int64_t shm_offset) override {
    LOG(FATAL) << "not implement";
  }

  std::string GetInfo() const override {
    std::string info;
    info.append(folly::stringPrintf("allocated/memory_size/util: %ld/%ld/%ld\n",
                                    allocated_.load(), memory_size_,
                                    allocated_.load() * 100 / memory_size_));
    return info;
  }

  uint64_t total_malloc() const override { return nr_malloc_; }

  bool Healthy() const override { return true; }

  void GetMallocsAppend(std::vector<char *> *mallocs_data) const override {
    LOG(FATAL) << "not implement";
  }
  void GetMallocsAppend(std::vector<int64> *mallocs_offset) const override {
    LOG(FATAL) << "not implement";
  }
  void Initialize() override { allocated_.store(0); }

  char *GetMallocData(int64 offset) const override { return data_ + offset; }

  int GetMallocSize(int64 offset) const override {
    return meta_data_block_[offset / slab_size_];
  }

  int64 GetMallocOffset(const char *data) const override {
    int64 offset = data - data_;
    CHECK(data_ <= data);
    CHECK(data + slab_size_ < data_ + memory_size_);
    return offset;
  }
  int GetMallocSize(const char *data) const override {
    return GetMallocSize(GetMallocOffset(data));
  }

 private:
  ShmFile shm_file_;
  std::atomic<uint64_t> allocated_{0};
  std::atomic<uint64_t> nr_malloc_{0};
  char *data_;
  uint64_t *meta_data_block_;
  int64 memory_size_;
  int64 meta_data_memory_size_;
  int64 slab_size_;
  DISALLOW_COPY_AND_ASSIGN(PersistSimpleMalloc);
};

template <bool PERFECT_FIT_MOD = true>
class PersistMemoryPool : public MallocApi {
  static constexpr uint64_t kChunkSize = 2 * 1024 * 1024LL;
  static constexpr int kMetaDataSize = 8;

  class Chunk {
   public:
    Chunk(char *chunk_start, uint64 chunk_id) {
      header_ = (ChunkHeader *)chunk_start;
      data_ = nullptr;
      chunk_id_ = chunk_id;
      allocated_entries_ = 0;
    }

    void Initialize() { header_->is_used = false; }

    void Use(int slab_size) {
      // header(nr_entries) + nr_entries * slab_size < kChunkSize
      int nr_entries = kChunkSize / slab_size;
      nr_entries -= (nr_entries % 64);

      while (ChunkHeader::HeaderSize(nr_entries) + nr_entries * slab_size >
             kChunkSize) {
        nr_entries -= 64;
      }

      CHECK_GT(nr_entries, 0);

      header_->Initialize(slab_size, nr_entries);
      data_ = (char *)header_ + header_->HeaderSize();
      // TODO: flush the meta data
      allocated_entries_ = 0;
      is_recovered = true;
    }

    bool ValidPointerRange(void *start, void *end) {
      if ((char *)end - (char *)start != header_->slab_size_) {
        return false;
      }
      if (start < data_ ||
          end > data_ + header_->slab_size_ * header_->nr_entries_) {
        return false;
      }
      return true;
    }

    void Recovery() {
      if (UNLIKELY(!is_recovered.load())) {
        allocated_entries_ = header_->bitmap_->NumberOfOnes();
        data_ = (char *)header_ + header_->HeaderSize();
        is_recovered = true;
      }
    }

    bool IsChunkUsed() const { return header_->is_used; }

    bool Full() const {
#ifdef DEBUG
      CHECK_EQ(header_->bitmap_->FirstZeroPos() == -1,
               allocated_entries_ == MaxEntryNumber());
#endif
      return allocated_entries_ == MaxEntryNumber();
      // return header_->bitmap_->FirstZeroPos() == -1;
    }

    char *Malloc() {
      base::AutoLock lock(lock_);
      Recovery();
      int entry_id = header_->bitmap_->SetZeroPos();
      if (entry_id == -1) return nullptr;
      allocated_entries_++;
      return Entry(entry_id);
    }

    void Free(void *memory_data) {
      base::AutoLock lock(lock_);
      Recovery();
#ifdef DEBUG
      CHECK_LE(slab_size_ + (char *)memory_data, data_ + kChunkSize);
      CHECK_GE((char *)memory_data, data_);
      CHECK(header_->bitmap_->Get(EntryId(memory_data)) == false);
#endif
      allocated_entries_--;
      header_->bitmap_->Clear(EntryId(memory_data));
    }

    int SlabSize() const { return header_->slab_size_; }

    int AllocatedEntryNumber() const { return allocated_entries_; }

    std::vector<int> GetMallocedIds() const {
      std::vector<int> return_id;
      for (int i = 0; i < MaxEntryNumber(); i++)
        if (header_->bitmap_->Get(i)) {
          return_id.push_back(i);
        }
      CHECK_EQ(return_id.size(), allocated_entries_)
          << fmt::format("chunk id is {}", chunk_id_);
      return return_id;
    }

    std::vector<char *> GetMallocedData() const {
      std::vector<char *> return_data;
      for (int i = 0; i < MaxEntryNumber(); i++)
        if (header_->bitmap_->Get(i)) {
          return_data.push_back(Entry(i));
        }
      CHECK_EQ(return_data.size(), allocated_entries_)
          << fmt::format("chunk id is {}", chunk_id_);
      return return_data;
    }

   private:
    FOLLY_ALWAYS_INLINE int MaxEntryNumber() const {
      return header_->nr_entries_;
    }

    int EntryId(void *memory_data) {
#ifdef DEBUG
      CHECK_EQ((memory_data - data_) % SlabSize(), 0);
#endif
      return ((char *)memory_data - data_) / SlabSize();
    }

    char *Entry(int entry_id) const {
#ifdef DEBUG
      CHECK_GE(entry_id, 0);
      CHECK_LT(entry_id, MaxEntryNumber());
#endif
      return data_ + entry_id * SlabSize();
    }

    struct ChunkHeader {
      void Initialize(int slab_size, int nr_entries) {
        slab_size_ = slab_size;
        nr_entries_ = nr_entries;
        new (bitmap_) BitMap(nr_entries);
        bitmap_->Clear();
        is_used = true;
      }

      bool is_used = false;
      int slab_size_;
      int nr_entries_;
      bool allocated;
      base::BitMap bitmap_[0];  // bitmap_ must be always in the tail

      size_t HeaderSize() const {
        return sizeof(ChunkHeader) + base::BitMap::MemorySize(nr_entries_);
      }
      static size_t HeaderSize(int nr_entires) {
        return sizeof(ChunkHeader) + base::BitMap::MemorySize(nr_entires);
      }
      DISALLOW_COPY_AND_ASSIGN(ChunkHeader);
    };
    ChunkHeader *header_;
    char *data_;
    std::atomic_bool is_recovered = false;
    int allocated_entries_;
    uint64 chunk_id_;
    base::Lock lock_;
    DISALLOW_COPY_AND_ASSIGN(Chunk);
  };

 public:
  PersistMemoryPool(const std::string &filename, int64 memory_size,
                    const std::vector<int> &slab_sizes)
      : allocated_slab_sizes_(slab_sizes.begin(), slab_sizes.end()) {
    bool file_exists = base::file_util::PathExists(filename);
    if (!shm_file_.Initialize(filename, memory_size)) {
      file_exists = false;
      CHECK(base::file_util::Delete(filename, false));
      CHECK(shm_file_.Initialize(filename, memory_size))
          << filename << " " << memory_size;
    }

    memory_size -= memory_size % kChunkSize;
    nr_chunks_ = memory_size / kChunkSize;

    CHECK_GE(nr_chunks_, slab_sizes.size());

    for (int64 i = 0; i < nr_chunks_; i++) {
      chunks_.push_back(new Chunk(shm_file_.Data() + i * kChunkSize, i));
    }

    for (auto slab : slab_sizes) {
      size_to_chunks_[slab] = new std::deque<Chunk *>();
    }

    if (!file_exists || !Valid()) {
      LOG(INFO) << "PersistMemoryPool: first initialization.";
      for (int i = 0; i < nr_chunks_; i++) {
        chunks_[i]->Initialize();
        free_chunks_.push_back(chunks_[i]);
      }
      return;
    }
    LOG(INFO) << "PersistMemoryPool: Recovery from shutdown.";
    for (int i = 0; i < nr_chunks_; i++) {
      auto each = chunks_[i];
      if (each->IsChunkUsed()) {
        auto it = size_to_chunks_.find(each->SlabSize());
        size_to_chunks_[each->SlabSize()]->push_back(each);
        each->Recovery();
        LOG(INFO) << "each->AllocatedEntryNumber() = "
                  << each->AllocatedEntryNumber();
        total_malloc_ += each->AllocatedEntryNumber();
      } else {
        free_chunks_.push_back(each);
      }
    }
  }

  bool Valid() const { return true; }

  char *New(int memory_size) override {
    if (PERFECT_FIT_MOD) {
#ifdef DEBUG
      CHECK(allocated_slab_sizes_.find(memory_size) !=
            allocated_slab_sizes_.end());
#endif
      return NewInternal(memory_size);
    } else {
      auto iter =
          allocated_slab_sizes_.lower_bound(kMetaDataSize + memory_size);
      CHECK(iter != allocated_slab_sizes_.end());
      char *ptr = NewInternal(*iter);
      *(int *)ptr = memory_size;
      return ptr + kMetaDataSize;
    }
  }

  char *NewInternal(int slab_size) {
    // TODO: fine grained lock
    base::AutoLock lock(lock_);
    void *return_ptr = nullptr;

    Chunk *last_used_chunk = nullptr;
    auto iter = size_to_last_used_chunk_.find(slab_size);
    if (iter != size_to_last_used_chunk_.end()) last_used_chunk = iter->second;

    if (last_used_chunk && !last_used_chunk->Full()) {
      return_ptr = last_used_chunk->Malloc();
      CHECK(return_ptr);
    } else if (!free_chunks_.empty()) {
      auto front = free_chunks_.front();
      free_chunks_.pop_front();
      front->Use(slab_size);
      auto chunks = size_to_chunks_[slab_size];
      chunks->push_back(front);
      size_to_last_used_chunk_[slab_size] = front;
      return_ptr = front->Malloc();
    } else {
      auto chunks = size_to_chunks_[slab_size];
      for (auto chunk : *chunks) {
        if (chunk->Full()) continue;
        return_ptr = chunk->Malloc();
        goto final;
      }
      LOG(FATAL) << fmt::format("Persist Memory Pool OOM, total_malloc={}",
                                total_malloc_);
    }
  final:
    CHECK(return_ptr);
    total_malloc_++;
    return (char *)return_ptr;
  }

  bool Free(void *memory_data) override {
    if (!PERFECT_FIT_MOD) memory_data = (char *)memory_data - kMetaDataSize;

    int chunk_id = GetChunkID(memory_data);
    if (0 <= chunk_id && chunk_id < chunks_.size()) {
      chunks_[chunk_id]->Free(memory_data);
      total_malloc_--;
      return true;
    } else {
      return false;
    }
  }
  void GetMallocsAppend(std::vector<char *> *mallocs_data) const override {
    for (auto chunk : chunks_) {
      auto chunk_data = chunk->GetMallocedData();
      for (char *each : chunk_data) {
        if (PERFECT_FIT_MOD)
          mallocs_data->push_back(each);
        else
          mallocs_data->push_back((char *)each + kMetaDataSize);
      }
    }
  }
  void GetMallocsAppend(std::vector<int64> *mallocs_offset) const override {
    std::vector<char *> mallocs_data;
    GetMallocsAppend(&mallocs_data);
    for (auto each : mallocs_data) {
      mallocs_offset->push_back(each - shm_file_.Data());
    }
  }
  void Initialize() override {}

  std::string GetInfo() const override {
    uint64 malloced_bytes = 0;
    uint64 malloced_chunk = 0;
    for (auto each : chunks_) {
      if (each->IsChunkUsed()) {
        malloced_bytes += each->AllocatedEntryNumber() * each->SlabSize();
        malloced_chunk++;
      }
    }
    return folly::sformat(
        "[PersistMemoryPool] "
        "Use {} of {} chunks,  Util {} %",
        malloced_chunk, chunks_.size(),
        100 * malloced_bytes / (kChunkSize * chunks_.size()));
  }

  char *GetMallocData(int64 offset) const override {
    return shm_file_.Data() + offset;
  }

  int GetMallocSize(int64 offset) const override {
    return GetMallocSize(GetMallocData(offset));
  }

  int64 GetMallocOffset(const char *data) const override {
    auto ret = data - shm_file_.Data();
    // see cache.SetShmMallocOffset
    CHECK_EQ((ret >> 3) << 3, ret);
    return ret;
  }

  int GetMallocSize(const char *data) const override {
    if (PERFECT_FIT_MOD)
      return chunks_[GetChunkID(data)]->SlabSize();
    else
      return *(int *)(data - kMetaDataSize);
  }

  uint64_t total_malloc() const override { return total_malloc_; }

  ~PersistMemoryPool() override {
    for (auto p : chunks_) {
      delete p;
    }
    for (auto [size, q] : size_to_chunks_) {
      delete q;
    }
  }

  bool Healthy() const { return true; }
  void AddMallocs4Recovery(int64_t shm_offset) {
    FB_LOG_EVERY_MS(ERROR, 1000) << "AddMallocs4Recovery not implement";
  }

 private:
  int64 GetChunkID(void *data) const { return GetChunkID((const char *)data); }
  int64 GetChunkID(const char *data) const {
    return GetChunkID(data - shm_file_.Data());
  }

  int64 GetChunkID(int64 offset) const { return offset / kChunkSize; }

 private:
  ShmFile shm_file_;
  int64 nr_chunks_;
  std::vector<Chunk *> chunks_;
  std::deque<Chunk *> free_chunks_;

  std::unordered_map<int, std::deque<Chunk *> *> size_to_chunks_;
  std::unordered_map<int, Chunk *> size_to_last_used_chunk_;

  std::set<int> allocated_slab_sizes_;

  std::atomic<int64> total_malloc_ = 0;

  base::Lock lock_;
  // different slab
};
}  // namespace base
