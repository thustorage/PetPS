#pragma once
#include "base/base.h"
#include "base/log.h"

static const int kCACHELINE_SIZE = 64;

#define IF_Persistence(something)                                              \
  if (Persistence) {                                                           \
    something                                                                  \
  }

namespace base {
class FENCE {
public:
  static void mfence() { asm volatile("mfence" ::: "memory"); }
  static void lfence() { asm volatile("lfence" ::: "memory"); }
  static void sfence() { asm volatile("sfence" ::: "memory"); }
};

inline void clflushopt(void *addr) {
  return;
  static int isClflushoptEnabled = !system("lscpu |grep clflushopt >/dev/null");
  static int isClwbEnabled = !system("lscpu |grep clfw >/dev/null");
  static int isClflushEnabled = !system("lscpu |grep clflush >/dev/null");
  if (isClflushoptEnabled) {
    asm volatile(".byte 0x66; clflush %0" : "+m"(*(volatile char *)(addr)));
  } else if (isClwbEnabled) {
    asm volatile(".byte 0x66; xsaveopt %0" : "+m"(*(volatile char *)(addr)));
  } else if (isClflushEnabled) {
    asm volatile("clflush %0" : "+m"(*(volatile char *)addr));
  } else {
    LOG(FATAL) << "system does't support clflush-like instrs";
    return;
  }
}

inline void clflushopt_range(void *des, size_t size) {
  char *addr = reinterpret_cast<char *>(des);
  size = size + ((uint64)(addr) & (kCACHELINE_SIZE - 1));
  for (size_t i = 0; i < size; i += kCACHELINE_SIZE) {
    clflushopt(addr + i);
  }
  FENCE::sfence();
}
}; // namespace base
