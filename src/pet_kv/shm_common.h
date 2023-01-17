#pragma once
#include <functional>

#include "base/async_time.h"
#include "memory/malloc.h"

namespace base {

typedef std::function<uint64()> TimestampGetter;

struct PetKVReadData {
  uint32 expire_timet = 0;
  const char *data = nullptr;
  int size = 0;
};

#pragma pack(push, 1)
struct PetKVData {
  struct Config {
    uint64_t kExpireReductBit;
    uint64_t kExpireReductMark;
    uint64_t kExpireBit;
    uint64_t kExpireMark;
    uint64_t kNoMallocOffset;
    explicit Config(int offset_bit = 32) {
      CHECK_GE(offset_bit, 32);
      kExpireBit = 64 - offset_bit;
      kExpireReductBit = 32 - kExpireBit;
      kExpireMark = (1LL << kExpireBit) - 1;
      kExpireReductMark = (1LL << kExpireReductBit) - 1;
      kNoMallocOffset = (1LL << offset_bit) - 1;
    }
  };
  static Config kConfig;

  PetKVData() = default;

  explicit PetKVData(int64_t offset) { SetShmMallocOffset(offset); }

  inline int64_t shm_malloc_offset() const {
    auto offset = data_value >> kConfig.kExpireBit;
    return offset == kConfig.kNoMallocOffset ? -1 : offset << 3;
  }

  inline void SetShmMallocOffset(int64_t shm_malloc_offset) {
    shm_malloc_offset = shm_malloc_offset == -1 ? kConfig.kNoMallocOffset
                                                : shm_malloc_offset >> 3;
    data_value = (data_value & kConfig.kExpireMark) |
                 (shm_malloc_offset << kConfig.kExpireBit);
  }

  inline uint32 expire_timet() const {
    return (data_value & kConfig.kExpireMark) << kConfig.kExpireReductBit;
  }

  inline void DoFlush() { clflushopt_range(&data_value, sizeof(uint64_t)); }

  uint64_t data_value = 0;
};
#pragma pack(pop)

}  // namespace base