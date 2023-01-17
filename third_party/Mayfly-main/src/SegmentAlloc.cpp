#include "SegmentAlloc.h"

namespace kv {

char *SegmentAlloc::alloc_segment(bool is_primary, bool gc_produce) {

  char *res;

retry:
  while (seg_free_list.l.empty()) {
  }

  seg_free_list.lock.wLock();

  if (seg_free_list.l.empty()) {
    seg_free_list.lock.wUnlock();
    printf("No free segs\n");
    sleep(1);
    goto retry;
  }

  res = seg_free_list.l.front();
  seg_free_list.l.pop_front();

  seg_free_list.lock.wUnlock();

  this->global_meta_mark_using(res, is_primary, gc_produce);
  reset_gc_stats(res);

  assert((uint64_t)res % SegmentAlloc::kSegSize == 0);

  // printf("get new segment %ld, is_primary %d, gc produce %d\n",
  // get_seg_id(res),
  //        is_primary, gc_produce);

  return res;
}

void SegmentAlloc::free_segment(char *segment) {
  assert((uint64_t)segment % kSegSize == 0);
  seg_free_list.lock.wLock();
  seg_free_list.l.push_back(segment);
  seg_free_list.lock.wUnlock();
}

} // namespace kv
