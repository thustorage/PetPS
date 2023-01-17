#ifndef CCEH_H_
#define CCEH_H_

#include <hash_api.h>

#define NOUSEVMEM 1
#define INPLACE 1

#ifdef NOUSEVMEM
  #include "cceh_allocator.h"
  using namespace cceh_allocator;
#else
  #include <vmem.h>
#endif

#include <bitset>
#include <cassert>
#include <cmath>
#include <iostream>
#include <thread>
#include <unordered_map>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "pair.h"
#include "persist.h"

using namespace cceh_allocator;
using namespace hasheval;

constexpr size_t kSegmentBits = 8;
constexpr size_t kMask = (1 << kSegmentBits) - 1;
constexpr size_t kShift = kSegmentBits;
// constexpr size_t kSegmentSize = (1 << kSegmentBits) * 16 * 4;
// NOTE(fyy)
constexpr size_t kSegmentSize = (1 << kSegmentBits) * 16 * 4;
constexpr size_t kNumPairPerCacheLine = 4;
constexpr size_t kNumCacheLine = 4;
struct Segment
{
  static const size_t kNumSlot = kSegmentSize / sizeof(Pair);

  Segment(void) : local_depth{0} {}

  Segment(size_t depth) : local_depth{depth} {}

  ~Segment(void) {}

  void *operator new(size_t size) { 
 #ifdef NOUSEVMEM
    void * ptr;
    cceh_allocator::Allocator::Allocate(&ptr, size);
    return ptr;
  #else
    return vmem_aligned_alloc(vmp, 64, size); 
  #endif
  }

  void *operator new[](size_t size)
  {
 #ifdef NOUSEVMEM
    void * ptr;
    cceh_allocator::Allocator::Allocate(&ptr, size);
    return ptr;
  #else
    return vmem_aligned_alloc(vmp, 64, size);
  #endif
  }

  int Insert(Key_t &, Value_t, size_t, size_t);
  void Insert4split(Key_t &, Value_t, size_t);
  bool Put(Key_t &, Value_t, size_t);
  Segment **Split(void);

  Pair _[kNumSlot];
  size_t local_depth;
  int64_t sema = 0;
  size_t pattern = 0;
  size_t numElem(void);
};

struct Directory
{
  static const size_t kDefaultDepth = 10;
  Segment **_;
  size_t capacity;
  size_t depth;
  bool lock;
  int sema = 0;
  void *operator new(size_t size) { 
   #ifdef NOUSEVMEM
    void * ptr;
    cceh_allocator::Allocator::Allocate(&ptr, size);
    return ptr;
    #else
    return vmem_aligned_alloc(vmp, 64, size); 
    #endif
  }

  void *operator new[](size_t size)
  {
   #ifdef NOUSEVMEM
    void * ptr;
    cceh_allocator::Allocator::Allocate(&ptr, size);
    return ptr;
    #else
    return vmem_aligned_alloc(vmp, 64, size);
    #endif
  }
  Directory(void)
  {
    depth = kDefaultDepth;
    capacity = pow(2, depth);
    // cceh_allocator::Allocator::Allocate((void **) &_, sizeof(void *));
    void *p;
    cceh_allocator::Allocator::Allocate((void **) &p, capacity * sizeof(Segment));
    _ = (Segment **)p;
    lock = false;
    sema = 0;
  }

  Directory(size_t _depth)
  {
    depth = _depth;
    capacity = pow(2, depth);
    // _ = new Segment *[capacity];
    // cceh_allocator::Allocator::Allocate((void **) &_, sizeof(void *));
    void *p;
    cceh_allocator::Allocator::Allocate((void **) &p, capacity * sizeof(Segment));
    _ = (Segment **)p;
    lock = false;
    sema = 0;
  }

  ~Directory(void) { 
   #ifdef NOUSEVMEM
    #else
    vmem_free(vmp, _); 
    #endif
  }

  bool Acquire(void)
  {
    bool unlocked = false;
    return CAS(&lock, &unlocked, true);
  }

  bool Release(void)
  {
    bool locked = true;
    return CAS(&lock, &locked, false);
  }

  void SanityCheck(void *);
  void LSBUpdate(int, int, int, int, Segment **);
};

class CCEH : public hash_api
{
public:
  CCEH(void);
  CCEH(size_t);
  void exist_flush();
  // ~CCEH(void);
  bool Insert(Key_t &, Value_t);
  bool InsertOnly(Key_t &, Value_t);
  bool Delete(Key_t &);
  Value_t Get(Key_t &);
  Value_t FindAnyway(Key_t &);
  hash_Utilization Utilization(void);
  float Space_utilization();
  size_t Capacity(void);
  bool Recovery(void);
  void lock_initialization(void);
  std::atomic<int> rehash_counter;
  // void* operator new(size_t size) { return vmem_aligned_alloc(vmp, 64, size);
  // }

  // hash_api
  void vmem_print_api() { 
   #ifdef NOUSEVMEM
    #else
    vmem_print(); 
    #endif
    }
  std::string hash_name() { exist_flush(); return "CCEH"; };
  bool recovery() { return Recovery(); }
  hash_Utilization utilization() { return Utilization(); }
  bool find(const char *key, size_t key_sz, char *value_out, unsigned tid)
  {
    Key_t k = *reinterpret_cast<const Key_t *>(key);
    auto r = Get(k);
    memcpy(value_out, (char*)&r, sizeof(uint64_t));
    return r;
  }

  bool insert(const char *key, size_t key_sz, const char *value,
              size_t value_sz, unsigned tid, unsigned t)
  {
    Key_t k = *reinterpret_cast<const Key_t *>(key);
    Insert(k, (char *) (*(uint64_t*)value));
    return true;
  }
  bool insertResize(const char *key, size_t key_sz, const char *value,
                    size_t value_sz)
  {
    Key_t k = *reinterpret_cast<const Key_t *>(key);
    return Insert(k, value);
  }
  bool update(const char *key, size_t key_sz, const char *value,
              size_t value_sz)
  {
    return true;
  }

  bool remove(const char *key, size_t key_sz, unsigned tid)
  {
    Key_t k = *reinterpret_cast<const Key_t *>(key);
    Delete(k);
    return true;
  }

  int scan(const char *key, size_t key_sz, int scan_sz, char *&values_out)
  {
    return scan_sz;
  }

private:
  size_t global_depth;
  Directory *dir;
};

#endif // EXTENDIBLE_PTR_H_
