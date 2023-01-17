#ifndef _SOFT_HASH_TABLE_H_
#define _SOFT_HASH_TABLE_H_

#include <cmath>
#include <cstring>

#include "SOFTList.h"
#include "hash.h"
#include "hash_api.h"
#include "utilities.h"
#define BUCKET_NUM 16777216
template <class T>
class SOFTHashTable : public hash_api
{
public:
  SOFTHashTable() { thread_ini(-1); }
  bool insert(uintptr_t k, T item)
  {
    SOFTList<T> &bucket = getBucket(k);
    return bucket.insert(k, item);
  }

  bool remove(uintptr_t k)
  {
    SOFTList<T> &bucket = getBucket(k);
    return bucket.remove(k);
  }

  T *contains(uintptr_t k)
  {
    SOFTList<T> &bucket = getBucket(k);
    return bucket.contains(k);
  }

  // hash_api
  // only during insert
  void forsoft()
  {
    for (int i = 0; i < BUCKET_NUM; i++)
      table[i] = SOFTList<T>();
  }
  hash_Utilization utilization()
  {
    hash_Utilization u;
    // uint64_t count = 0;
    // uint64_t bn = 0;
    // for (int i = 0; i < BUCKET_NUM; i++) {
    //   SOFTList<T>& bucket = table[i];
    //   auto h = bucket.head->next.load();
    //   while (h) {
    //     bn++;
    //     h = h->next.load();
    //   }
    // }
    // auto lf = (float)bn / BUCKET_NUM;
    // u.load_factor = (lf > 1 ? 1 : lf) * 100.0;
    // u.utilization =
    //     (float)bn * 16 / ((sizeof(PNode<T>) + sizeof(Node<T>)) * bn) * 100.0;
    return u;
  };
  void vmem_print_api() { std::cout<<"tisss:"<<tisss<<std::endl; }
  std::string hash_name() { return "SOFT"; };
  bool recovery()
  {
    SOFTrecovery();
    return true;
  }
  void thread_ini(int id)
  {
    init_alloc(id);
    init_volatileAlloc(id);
  }

  bool find(const char *key, size_t key_sz, char *value_out, unsigned tid)
  {
    uintptr_t k = *reinterpret_cast<const uintptr_t *>(key);
    auto r = contains(k);

    return r;
  }

  bool insert(const char *key, size_t key_sz, const char *value,
              size_t value_sz, unsigned tid, unsigned t)
  {
    uintptr_t k = *reinterpret_cast<const uintptr_t *>(key);
    T v = *reinterpret_cast<const T *>(value);
    return insert(k, v);
  }

  bool update(const char *key, size_t key_sz, const char *value,
              size_t value_sz)
  {
    return true;
  }

  bool remove(const char *key, size_t key_sz, unsigned tid)
  {
    uintptr_t k = *reinterpret_cast<const uintptr_t *>(key);
    remove(k);
    return true;
  }

  int scan(const char *key, size_t key_sz, int scan_sz, char *&values_out)
  {
    return scan_sz;
  }

private:
  SOFTList<T> &getBucket(uintptr_t k)
  {
    return table[std::abs((long long)h((void *)&k, sizeof(k)) % BUCKET_NUM)];
  }
  void SOFTrecovery()
  {
    // forsoft();
    auto curr = alloc->mem_chunks;
    for (; curr != nullptr; curr = curr->next)
    {
      PNode<T> *currChunk = static_cast<PNode<T> *>(curr->obj);
      uint64_t numOfNodes = SSMEM_DEFAULT_MEM_SIZE / sizeof(PNode<T>);
      for (uint64_t i = 0; i < numOfNodes; i++)
      {
        PNode<T> *currNode = currChunk + i;
        // if (currNode->key == 0) continue;
        if (currNode->isDeleted())
        {
          // currNode->validStart = currNode->validEnd.load();
          // ssmem_free(alloc, currNode, true);
        }
        else
        {
          bool pValid = currNode->recoveryValidity();
          uintptr_t key = currNode->key;
          SOFTList<T> &bucket = getBucket(key);
          bucket.quickInsert(currNode, pValid);
        }
      }
    }
  }
  SOFTList<T> table[BUCKET_NUM];
};

#endif