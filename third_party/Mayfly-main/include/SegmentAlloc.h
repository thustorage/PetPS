#if !defined(_SEGMENT_ALLOC_H_)
#define _SEGMENT_ALLOC_H_

#include "Common.h"
#include "NVM.h"
#include "Rdma.h"
#include <list>

namespace kv {

struct alignas(define::kCacheLineSize) SegList {
  WRLock lock;
  std::list<char *> l;
};

enum SegStatus : uint8_t {
  EMPTY = 0,
  USING,
  USED,
};
struct SegMeta {
  // for foreground, cacheline alignment;
  // for gc, log entries are more compact without alignment
  bool gc_produce;

  bool is_primary;
  SegStatus status;
  uint64_t sequence_num;
};

struct GlobalHeader {
  uint64_t uuid;
  uint64_t heartbeat;
};

class SegmentAlloc {
private:
  uint16_t node_id;
  char *start_addr;
  uint64_t space_size;

  char *seg_start_addr;

  SegList seg_free_list;

  std::atomic<uint32_t> *garbage_bytes_stat;

  GlobalHeader *global_header;
  SegMeta *global_meta;

public:
  const static uint64_t kMaxPMSpace = 1024 * define::GB;
  const static uint64_t kSegSize = 4 * define::MB;
  const static uint64_t kMaxSgeCnt = kMaxPMSpace / kSegSize;
  const static uint64_t kGlobalMetaSize = 1 * define::GB;
  const static uint64_t kGlobalMetaHeader = 4 * define::KB;
  const static uint64_t kUUID = 0x19970527; // to detect valid PM space

  static_assert(kSegSize == kStrideSegSize, "XXXX");
  static_assert(kMaxSgeCnt * sizeof(SegMeta) + kGlobalMetaHeader <
                    kGlobalMetaSize,
                "XXXX");

  inline SegMeta *get_seg_global_meta() { return global_meta; }

  inline uint64_t get_seg_id(char *addr) {
    return (addr - seg_start_addr) / kSegSize;
  }

  inline char *get_seg_addr_by_id(uint64_t seg_id) {
    return seg_start_addr + seg_id * kSegSize;
  }

  void update_gc_stats(char *log_entry_addr, size_t size) {
    garbage_bytes_stat[get_seg_id(log_entry_addr)].fetch_add(
        size, std::memory_order::memory_order_relaxed);
  }

  void reset_gc_stats(char *seg) {
    garbage_bytes_stat[get_seg_id(seg)].store(0);
  }

  SegMeta *get_seg_meta(char *seg) { return &global_meta[get_seg_id(seg)]; }

  void global_meta_mark_using(char *seg, bool is_priamry, bool gc_produce) {
    static std::atomic<uint64_t> global_sequence_num{0};
    auto sequence_num = global_sequence_num.fetch_add(0);
    auto seg_id = get_seg_id(seg);

    auto &meta = global_meta[seg_id];

    assert(meta.status == SegStatus::EMPTY);

    meta.sequence_num = sequence_num;
    meta.gc_produce = gc_produce;
    meta.is_primary = is_priamry;
    meta.status = SegStatus::USING;

    clwb(&meta);
  }

  void global_meta_mark_used(char *seg) {
    auto seg_id = get_seg_id(seg);
    auto &meta = global_meta[seg_id];

    assert(meta.status == SegStatus::USING);

    meta.status = SegStatus::USED;

    clwb(&meta);
  }

  void global_meta_mark_free(char *seg) {
    auto seg_id = get_seg_id(seg);
    auto &meta = global_meta[seg_id];

    assert(meta.status == SegStatus::USED);

    meta.status = SegStatus::EMPTY;

    clwb(&meta);
  }

  SegmentAlloc(uint16_t node_id, char *start_addr, uint64_t space_size,
               bool is_recovery = false)
      : node_id(node_id), space_size(space_size) {

    garbage_bytes_stat = new std::atomic<uint32_t>[kMaxSgeCnt];

    global_header = (GlobalHeader *)start_addr;
    global_meta = (SegMeta *)(start_addr + kGlobalMetaHeader);

    seg_start_addr = start_addr + kGlobalMetaHeader;
    seg_start_addr = (char *)(((uint64_t)seg_start_addr + kSegSize - 1) /
                              kSegSize * kSegSize);

    if (is_recovery == true) {
      return;
    }

    global_header->uuid = kUUID;
    global_header->heartbeat = 0;
    clwb_range(global_header, sizeof(global_header));

    for (uint64_t i = 0; i < kMaxSgeCnt; ++i) {
      global_meta[i].status = SegStatus::EMPTY;
    }

    clwb_range(start_addr, kGlobalMetaHeader);

    for (char *seg = seg_start_addr; seg < start_addr + space_size;
         seg += kSegSize) {
      seg_free_list.l.push_back((char *)seg);
    }
  }

  ~SegmentAlloc() {
    if (garbage_bytes_stat) {
      delete[] garbage_bytes_stat;
    }
  }

  uint64_t *get_heartbeat_addr() { return &global_header->heartbeat; }

  char *alloc_segment(bool is_primary, bool gc_produce = false);
  void free_segment(char *segment);
};

} // namespace kv

#endif // _SEGMENT_ALLOC_H_
