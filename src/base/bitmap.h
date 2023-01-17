#pragma once
#include <cstdint>
#include <cassert>
#include <cstring>
#include <cstdio>

#include "base/log.h"

namespace base {

class BitMap {
 private:
  int n;
  uint64_t bits[0];

 public:
  static size_t MemorySize(int n) {
    CHECK_EQ(n % 64, 0);
    return sizeof(BitMap) + n / 64 * sizeof(uint64_t);
  }

  BitMap() {}

  BitMap(int n) : n(n) {
    CHECK_EQ(n % 64, 0);
    memset(bits, 0, n / 8);
  }

  bool Get(int pos) const {
    return (bits[pos / 64] >> (pos % 64)) & 0x1;
  }

  void Set(int pos) {
    uint64_t &v = bits[pos / 64];
    v = v | (0x1ull << (pos % 64));
  }

  int NumberOfOnes() const {
    int ret = 0;
    for (int i = 0; i < n; i++) {
      if (Get(i)) ret++;
    }
    return ret;
  }

  void Clear() {
    memset(bits, 0, n / 8);
  }

  void Clear(int pos) {
    uint64_t &v = bits[pos / 64];
    v = v & ~(0x1ull << (pos % 64));
  }

  int FirstZeroPos() const {
    for (int i = 0; i < n / 64; ++i) {
      uint64_t v = bits[i];
      uint64_t b = ~v;
      if (b) {
        uint64_t pos = __builtin_ctzll(b);
        return i * 64 + pos;
      }
    }
    return -1;
  }

  int SetZeroPos() {
    for (int i = 0; i < n / 64; ++i) {
      uint64_t &v = bits[i];
      uint64_t b = ~v;
      if (b) {
        uint64_t pos = __builtin_ctzll(b);
        v = v | (0x1ull << pos);
        return i * 64 + pos;
      }
    }
    return -1;
  }
};

}  // namespace base