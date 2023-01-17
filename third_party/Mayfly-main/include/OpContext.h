#if !defined(_OP_CONTEXT_H_)
#define _OP_CONTEXT_H_

#include "Common.h"
#include "DSM.h"

namespace kv {

class Index;
class LogEntry;

// for pipeline operations without coroutine
struct OpContext {
  bool is_put; // false for del
  Index *index;
  LogEntry *e;
  HashType hash;

  // client address
  NodeIDType node_id;
  ThreadIDType t_id;
  uint8_t rpc_tag;

  uint8_t trigger; // if trigger == replication factor, call callback

  char *dram_buf;

  // uint64_t send_lat;
};

class OpContextManager {
private:
  constexpr static int kCtxNr = 256;
  constexpr static int kBitMapSize = kCtxNr / 64;
  OpContext ctxs[kCtxNr];
  uint64_t bitmap[kBitMapSize];

  DSM *dsm;

public:
  OpContextManager(DSM *dsm) : dsm(dsm) {
    memset(bitmap, 0, sizeof(bitmap));
    for (size_t k = 0; k < kCtxNr; ++k) {
#ifndef BATCH_REQ
      ctxs[k].dram_buf = dsm->get_rdma_buffer() + k * MESSAGE_SIZE;
#else
      ctxs[k].dram_buf = dsm->get_rdma_buffer() + k * (8 * MESSAGE_SIZE);
#endif
    }

#ifdef BATCH_REQ
     bitmap[kBitMapSize - 1] = (0x1ull << 63);
#endif 

  }

  uint16_t alloc_ctx(OpContext **ctx) {

    for (int i = 0; i < kBitMapSize; ++i) {
      uint64_t b = ~bitmap[i];
      if (b == 0) {
        continue;
      }

      uint64_t pos = __builtin_ctzll(b);

      bitmap[i] = bitmap[i] | (0x1ull << pos);

      pos += 64 * i;
      *ctx = &ctxs[pos];

      (*ctx)->trigger = 2;
      return pos;
    }

    printf("not empty context\n");
    exit(-1);
  }

  void force_free_ctx(uint16_t idx) {

    int k = idx / 64;
    bitmap[k] = bitmap[k] & ~(0x1ull << idx);
  }

  bool trigger_ctx(uint16_t idx) {

    assert(idx < kCtxNr);
    assert(ctxs[idx].trigger > 0);

    if (--ctxs[idx].trigger == 0) {
      int k = idx / 64;
      bitmap[k] = bitmap[k] & ~(0x1ull << idx);

      return true;
    }
    // if (ctxs[idx].trigger == 1) {
    //   __builtin_prefetch(ctxs[idx].dram_buf);
    // }

    return false;
  }

  OpContext *get_ctx(uint16_t idx) {
    assert(idx < kCtxNr);
    return &ctxs[idx];
  }
};

} // namespace kv

#endif // _OP_CONTEXT_H_
