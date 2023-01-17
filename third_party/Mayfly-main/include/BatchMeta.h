#if !defined(_BATCH_REQ_H_)
#define _BATCH_REQ_H_

#include <stdint.h>

namespace kv {
struct BatchMeta {
  uint8_t batch_size;
  union {
    uint64_t wr_id;
    uint8_t ctx_ids[8];
  };

  char *buf;
  uint64_t remote_addr;
  uint64_t bytes;

  uint64_t start_ns;

  void clear() {
    wr_id = 0;
    batch_size = 0;
  }

} __attribute__((packed));

static_assert(sizeof(BatchMeta) == 41, "XXX");
} // namespace kv

#endif // _BATCH_REQ_H_
