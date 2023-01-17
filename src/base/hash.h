#pragma once
#include "base.h"
#include "city.h"

inline uint64 GetHash(uint64 key) {
  return ::CityHash64(reinterpret_cast<const char *>(&key), sizeof(uint64));
}

inline uint64 GetHashWithLevel(uint64 key, int level) {
  return ::CityHash64WithSeeds(reinterpret_cast<const char *>(&key),
                             sizeof(uint64), kInt32Max, level);
}

namespace base {

inline uint64 CityHash64(const char *buf, size_t len) {
  return ::CityHash64(buf, len);
}
}  // namespace base