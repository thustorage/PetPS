#if !defined(_NVM_H_)
#define _NVM_H_

namespace define {
constexpr uint16_t kCacheLineSize = 64;
}

inline void clflush(void *addr) {
#ifdef __x86_64__
  asm volatile("clflush %0" : "+m"(*(volatile char *)(addr)));
#endif
}

inline void clwb(void *addr) {
#ifdef __x86_64__
  asm volatile(".byte 0x66; xsaveopt %0" : "+m"(*(volatile char *)(addr)));
#endif
}

inline void clflushopt(void *addr) {
#ifdef __x86_64__
  asm volatile(".byte 0x66; clflush %0" : "+m"(*(volatile char *)(addr)));
#endif
}

inline void persistent_barrier() { 
  #ifdef __x86_64__
  asm volatile("sfence\n" : :); 
  #endif
  }

inline void compiler_barrier() { asm volatile("" ::: "memory"); }

inline void clflush_range(void *des, size_t size) {
  char *addr = (char *)des;
  size = size + ((uint64_t)(addr) & (define::kCacheLineSize - 1));
  for (size_t i = 0; i < size; i += define::kCacheLineSize) {
    clflush(addr + i);
  }
}

inline void clwb_range(void *des, size_t size) {
  char *addr = (char *)des;
  size = size + ((uint64_t)(addr) & (define::kCacheLineSize - 1));
  for (size_t i = 0; i < size; i += define::kCacheLineSize) {
    clwb(addr + i);
  }
  persistent_barrier();
}

inline void clflushopt_range(void *des, size_t size) {
  char *addr = (char *)des;
  size = size + ((uint64_t)(addr) & (define::kCacheLineSize - 1));
  for (size_t i = 0; i < size; i += define::kCacheLineSize) {
    clflushopt(addr + i);
  }
  persistent_barrier();
}

#endif // _NVM_H_
