#include "Index.h"
#include "Global.h"
#include "SegmentAlloc.h"
#include "murmur_hash2.h"

namespace kv {
Index::Index(char *start_addr) {
  auto bucket_size = Index::index_bucket_size();
  ht = (Bucket *)start_addr;
  memset((void *)ht, 0, bucket_size);

  assert((uint64_t)ht % 8 == 0);
}

Index::~Index() {}

Index::IndexIterator Index::begin() {
  IndexIterator iter;
  iter.bucket_pos = 0;
  iter.bucket_ptr = &ht[iter.bucket_pos];
  iter.slot_pos = 0;

  if (iter.bucket_ptr->slots[iter.slot_pos].tag == 0) {
    next(iter);
  }
  return iter;
}

LogEntry *Index::unref(IndexIterator &iter) {
  return (LogEntry *)iter.bucket_ptr->slots[iter.slot_pos].get_addr();
}

bool Index::next(IndexIterator &iter) {
  for (size_t i = iter.slot_pos + 1; i < kSlotPerBucket; ++i) {
    if (iter.bucket_ptr->slots[i].tag != 0) {
      iter.slot_pos = i;
      return true;
    }
  }

  if (iter.bucket_ptr->next) {
    iter.bucket_ptr = iter.bucket_ptr->next;
    iter.slot_pos = 0;
    return next(iter);
  }

  if (++iter.bucket_pos >= kBucketSize) {
    return false;
  }

  iter.bucket_ptr = &ht[iter.bucket_pos];
  iter.slot_pos = 0;
  return next(iter);
}

LogEntry *Index::get(const Slice &key) {

  uint64_t hash = MurmurHash64A(key.s, key.len);
  return get(key, hash);
}

LogEntry *Index::get(const Slice &key, HashType hash) {

  auto index = (hash >> 16) % kBucketSize;
  auto tag = (hash << 48) >> 48;
  tag = tag == 0 ? 1 : tag;

  auto &bucket = ht[index];

  auto *cur_bucket = &bucket;

next_bucket:
  for (int i = 0; i < kSlotPerBucket; ++i) {
    auto slot = cur_bucket->slots[i];
    if (slot.tag == tag) { // tag hit

      LogEntry *pair = (LogEntry *)slot.get_addr();
      if (pair->get_key().equal(key)) { // find key
        return pair;
      }
    }
  }

  if (cur_bucket->next != nullptr) {
    cur_bucket = cur_bucket->next;
    goto next_bucket;
  }

  return nullptr;
}

void Index::put(const Slice &key, LogEntry *e, VersionType ver) {
  uint64_t hash = MurmurHash64A(key.s, key.len);
  put(key, e, ver, hash);
}

void Index::put(const Slice &key, LogEntry *e, VersionType ver, HashType hash) {

  auto index = (hash >> 16) % kBucketSize;
  auto tag = (hash << 48) >> 48;
  tag = tag == 0 ? 1 : tag;

  auto &bucket = ht[index];

  auto &lock = bucket.lock;
  lock.wLock();

  auto *cur_bucket = &bucket;

  Slot *insert_slot = nullptr;
  Slot *empty_slot = nullptr;

  LogEntry *gc_entry = nullptr;

next_bucket:
  for (int i = 0; i < kSlotPerBucket; ++i) {
    auto &slot = cur_bucket->slots[i];
    if (slot.tag == tag) { // tag hit
      auto pair = (LogEntry *)slot.get_addr();
      if (pair->get_key().equal(key)) {

        if (pair->ver > ver) { // this update to index should be discarded
          lock.wUnlock();
          return;
        }

        insert_slot = &slot; // update
        gc_entry = pair;
        break;
      }
    } else if (empty_slot == nullptr && slot.val == 0) { // empty slot
      empty_slot = &slot;
    }
  }

  if (insert_slot == nullptr) {
    if (cur_bucket->next != nullptr) {
      cur_bucket = cur_bucket->next;
      goto next_bucket;
    }

    if (empty_slot == nullptr) { // overflow
      auto new_bucket = (Bucket *)malloc(sizeof(Bucket));
      memset((void *)new_bucket, 0, sizeof(Bucket));

      cur_bucket->next = new_bucket;
      empty_slot = &new_bucket->slots[0];
    }

    insert_slot = empty_slot;
  }

  Slot new_slot;
  new_slot.val = (uint64_t)e;
  new_slot.tag = tag;

  insert_slot->val = new_slot.val; // atomic write

  lock.wUnlock();

  // update gc stats
  if (gc_entry) {
    segment_alloc->update_gc_stats((char *)gc_entry,
                                   next_cache_line(gc_entry->size()));
  }
}

bool Index::del(const Slice &key, VersionType ver) {
  uint64_t hash = MurmurHash64A(key.s, key.len);
  return del(key, ver, hash);
}

bool Index::del(const Slice &key, VersionType ver, HashType hash) {

  auto index = (hash >> 16) % kBucketSize;
  auto tag = (hash << 48) >> 48;
  tag = tag == 0 ? 1 : tag;

  auto &bucket = ht[index];

  auto &lock = bucket.lock;

  lock.wLock();

  auto *cur_bucket = &bucket;

next_bucket:
  for (int i = 0; i < kSlotPerBucket; ++i) {
    Slot &slot = cur_bucket->slots[i];
    if (slot.tag == tag) { // tag hit
      LogEntry *pair = (LogEntry *)slot.get_addr();
      if (pair->get_key().equal(key)) { // find key

        slot.val = 0;

        lock.wUnlock();

        return true;
      }
    }
  }

  if (cur_bucket->next != nullptr) {
    cur_bucket = cur_bucket->next;
    goto next_bucket;
  }

  lock.wUnlock();
  return false;
}
} // namespace kv
