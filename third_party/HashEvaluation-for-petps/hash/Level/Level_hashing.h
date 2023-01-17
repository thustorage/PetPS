#ifndef LEVEL_HASHING_H_
#define LEVEL_HASHING_H_

#include "../common/hash_api.h"
#include <stdint.h>
#include "../common/vmem.h"


#include <cstring>
#include <mutex>
#include <shared_mutex>

#include "../common/pair.h"
#include "../common/persist.h"

#define NOUSEVMEM 1

#ifdef NOUSEVMEM
#include "level_allocator.h"
using namespace level_allocator;
#endif


using namespace hasheval;
#define ASSOC_NUM 3

struct Entry
{
  Key_t key;
  Value_t value;
  Entry()
  {
    key = INVALID;
    value = NONE;
  }
  void *operator new[](size_t size)
  {
   #ifdef NOUSEVMEM
    void * ptr;
    level_allocator::Allocator::Allocate(&ptr, size);
    return ptr;
    #else
    return vmem_aligned_alloc(vmp, 64, size);
    #endif
  }

  void *operator new(size_t size) { 
       #ifdef NOUSEVMEM
    void * ptr;
    level_allocator::Allocator::Allocate(&ptr, size);
    return ptr;
    #else
    return vmem_aligned_alloc(vmp, 64, size);
    #endif
   }
};

struct Node
{
  uint8_t token[ASSOC_NUM];
  Entry slot[ASSOC_NUM];
  char dummy[13];
  void *operator new[](size_t size)
  {
   #ifdef NOUSEVMEM
    void * ptr;
    level_allocator::Allocator::Allocate(&ptr, size);
    return ptr;
    #else
    return vmem_aligned_alloc(vmp, 64, size);
    #endif
  }

  void *operator new(size_t size) { 
       #ifdef NOUSEVMEM
    void * ptr;
    level_allocator::Allocator::Allocate(&ptr, size);
    return ptr;
    #else
    return vmem_aligned_alloc(vmp, 64, size);
    #endif
   }
};
class LevelHashing : public hash_api
{
private:
  Node *buckets[2];
  Node *interim_level_buckets;
  uint64_t level_item_num[2];

  uint64_t levels;
  uint64_t addr_capacity;
  uint64_t total_capacity;
  //    uint32_t occupied;
  uint64_t f_seed;
  uint64_t s_seed;
  uint32_t resize_num;
  int32_t resizing_lock = 0;
  std::shared_mutex *mutex;
  int nlocks;
  int locksize;

  void generate_seeds(void);
  void resize(void);
  int b2t_movement(uint64_t);
  uint8_t try_movement(uint64_t, uint64_t, Key_t &, Value_t);
  char *resizingPtr;

public:
  LevelHashing(void);
  LevelHashing(size_t);
  ~LevelHashing(void);
  bool InsertOnly(Key_t &, Value_t);
  bool Insert(Key_t &, Value_t);
  bool Delete(Key_t &);
  bool Recovery();
  Value_t Get(Key_t &);
  hash_Utilization Utilization(void);
  size_t Capacity(void)
  {
    return (addr_capacity + addr_capacity / 2) * ASSOC_NUM;
  }
  // hash_api
  void vmem_print_api() { vmem_print(); }
  std::string hash_name() { return "Level"; };
  bool recovery()
  {
    Recovery();
    return true;
  }
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
    // Insert(k, value);
    Insert(k, (char *) *(uint64_t*)value);
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
    return Delete(k);
  }

  int scan(const char *key, size_t key_sz, int scan_sz, char *&values_out)
  {
    return scan_sz;
  }
};

#endif // LEVEL_HASHING_H_
