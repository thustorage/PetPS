#if !defined(_INDEX_H_)
#define _INDEX_H_

#include "Common.h"
#include "LogEntry.h"
#include "Timer.h"
#include "WRLock.h"

#include <string>
#include <unordered_map>

namespace kv {

union Slot {
  struct {
    uint64_t addr : 48;
    uint64_t tag : 16;
  };
  uint64_t val;

  void *get_addr() {
    auto slot = *this;
    slot.tag = 0;

    return (void *)slot.val;
  }

  static Slot Null() {
    Slot s;
    s.val = 0;
    return s;
  }
};
static_assert(sizeof(Slot) == sizeof(uint64_t));

class Index {
  const static uint8_t kSlotPerBucket = 6;
  const static uint64_t kBucketSize = 1 << 20;

public:
  struct alignas(define::kCacheLineSize) Bucket {
    Slot slots[kSlotPerBucket];
    WRLock lock;
    Bucket *next;
  };

  struct IndexIterator {
    uint32_t bucket_pos;
    Bucket *bucket_ptr;
    uint8_t slot_pos;

    IndexIterator() : bucket_pos(0), bucket_ptr(nullptr), slot_pos(0) {}
  };
  static_assert(sizeof(Bucket) <= define::kCacheLineSize);

private:
  Bucket *ht;

public:
  Index(char *start_addr);
  ~Index();
  LogEntry *get(const Slice &key, HashType hash);
  LogEntry *get(const Slice &key);

  void put(const Slice &key, LogEntry *e, VersionType ver, HashType hash);
  void put(const Slice &key, LogEntry *e, VersionType ver);

  bool del(const Slice &key, VersionType ver, HashType hash);
  bool del(const Slice &key, VersionType ver);

  IndexIterator begin();
  LogEntry *unref(IndexIterator &iter);
  bool next(IndexIterator &iter);

  static uint64_t index_bucket_size() { return sizeof(Bucket) * kBucketSize; }
};
} // namespace kv

#endif // _INDEX_H_
