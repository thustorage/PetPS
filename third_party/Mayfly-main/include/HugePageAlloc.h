#ifndef __HUGEPAGEALLOC_H__
#define __HUGEPAGEALLOC_H__

#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>

#include <memory.h>
#include <sys/mman.h>

#include "Debug.h"
#include <string>

// #define USE_DRAM_FOR_PM

extern int global_socket_id;

char *getIP();
uint64_t get_dax_physical_addr(int numa_id);

inline void *hugePageAlloc(size_t size) {

  void *res = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
  if (res == MAP_FAILED) {
    Debug::notifyError("[1] %s mmap failed!\n", getIP());
  }

  return res;
}

inline void hugePageFree(void *ptr, size_t size) { munmap(ptr, size); }

inline void *pageAlloc(size_t size) {

  void *res = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
  if (res == MAP_FAILED) {
    Debug::notifyError("[2] %s mmap failed!\n", getIP());
  }

  return res;
}

#endif /* __HUGEPAGEALLOC_H__ */
