#ifndef __COMMON_H__
#define __COMMON_H__

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <atomic>
#include <bitset>

#include "Debug.h"
#include "HugePageAlloc.h"
#include "NVM.h"
#include "Rdma.h"

#include "Slice.h"
#include "WRLock.h"

// #define ENABLE_PROFILE

// #define VERBOSE_PRINT

using NodeIDType = uint8_t;
using ThreadIDType = uint8_t;

namespace kv {
using HashType = uint64_t;

using VersionType = uint32_t;

using CRCType = uint32_t;
using KeySizeType = uint16_t;
using ValueSizeType = uint32_t;

constexpr NodeIDType kServerNodeID = 0;

constexpr uint64_t kServerMemSize = 16; // GB
constexpr uint64_t kDPUMemSize = 1;     // GB

constexpr uint8_t kMaxNetThread = 120;
constexpr uint8_t kMaxDPUThread = 8;


} // namespace kv

constexpr char kMagicNumer = 0x12;

constexpr int kLogicCoreCnt = 36;
constexpr int kMaxSocketCnt = 2;

// #define MAX_MACHINE 8

#define ADD_ROUND(x, n) ((x) = ((x) + 1) % (n))

#define MESSAGE_SIZE 4096 // byte

#define RPC_QUEUE_SIZE 512

// { app thread

#define APP_MESSAGE_NR RPC_QUEUE_SIZE

// }

// { dir thread
#define NR_DIRECTORY 1

#define DIR_MESSAGE_NR RPC_QUEUE_SIZE
// }

extern int global_socket_id;
extern int core_table[kMaxSocketCnt][kLogicCoreCnt];
void bind_core(uint16_t core);
void auto_bind_core(int ServerConfig =0);
char *getIP();
char *getMac();
uint64_t get_dax_physical_addr(int numa_id);

inline int bits_in(std::uint64_t u) {
  auto bs = std::bitset<64>(u);
  return bs.count();
}

namespace define {

constexpr uint64_t KB = 1024ull;
constexpr uint64_t MB = 1024ull * 1024;
constexpr uint64_t GB = 1024ull * MB;
// for remote allocate
constexpr uint64_t kChunkSize = MB * 32;

constexpr uint64_t ns2ms = 1000ull * 1000ull;
constexpr uint64_t ns2s = ns2ms * 1000ull;

// lock on-chip memory
constexpr uint64_t kLockStartAddr = 0;
constexpr uint64_t kLockChipMemSize = 200 * 1024;

} // namespace define

static inline unsigned long long asm_rdtsc(void) {
#ifdef __x86_64__
  unsigned hi, lo;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
#else
  return 1;
#endif
}

static inline uint64_t next_cache_line(uint64_t v) {
  return (v + define::kCacheLineSize - 1) & (~(define::kCacheLineSize - 1));
}

struct NodeConfig {
  char rnic_name;
  int gid_index;
};

extern NodeConfig global_node_config[32];

#endif /* __COMMON_H__ */
