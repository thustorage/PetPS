#if !defined(_PRIMARY_LOG_MANAGER)
#define _PRIMARY_LOG_MANAGER

#include "Common.h"
#include "LogEntry.h"
#include <cstdint>

namespace kv {

class SegmentAlloc;

// per-worker-thread
class PrimaryLogManager {
private:
  SegmentAlloc *seg_alloc;
  uint16_t thread_id;

  char *running_seg;
  uint32_t seg_off;

  LogEntry *reserve_entry(size_t size);

public:
  PrimaryLogManager(SegmentAlloc *seg_alloc, uint16_t thread_id);
  ~PrimaryLogManager();

  LogEntry *alloc_entry_for_put(const Slice &key, const Slice &value,
                                VersionType ver, ShardID shard_id,
                                CRCType kv_crc, char *dram_buf);
  LogEntry *alloc_entry_for_del(const Slice &key, VersionType ver,
                               char *dram_buf);
};

} // namespace kv

#endif // _PRIMARY_LOG_MANAGER
