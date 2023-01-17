
// Copyright (c) Simon Fraser University & The Chinese University of Hong Kong.
// All rights reserved. Licensed under the MIT license.
#ifndef HASH_INTERFACE_H_
#define HASH_INTERFACE_H_
#include <hash_api.h>

#include <cstdio>

#include "allocator.h"
#ifdef PMEM
#include <libpmemobj.h>
#endif

/*
 * Parent function of all hash indexes
 * Used to define the interface of the hash indexes
 */
typedef size_t Key_t;
typedef const char *Value_t;
const Value_t NONE = 0x0;
const Value_t DEFAULT = reinterpret_cast<Value_t>(1);
/*variable length key*/
struct string_key
{
  int length;
  char key[0];
};

template <class T>
class Hash : public hash_api
{
public:
  Hash(void) = default;
  ~Hash(void) = default;
  virtual bool Insert(T, Value_t) = 0;
  virtual bool Insert(T, Value_t, bool) = 0;

  virtual void bootRestore(){

  };
  virtual void reportRestore(){

  };
  virtual bool Delete(T) = 0;
  virtual bool Delete(T, bool) = 0;
  virtual Value_t Get(T) = 0;
  virtual Value_t Get(T key, bool is_in_epoch) = 0;
  virtual bool Recovery() = 0;
  virtual void getNumber() = 0;
  virtual hash_Utilization utiliz();
  virtual string hash_name();
  // hash_api
  hash_Utilization utilization() { return utiliz(); };
  bool recovery() { return Recovery(); };
  bool find(const char *key, size_t key_sz, char *value_out, unsigned tid)
  {
    T k = *reinterpret_cast<const T *>(key);
    auto r = Get(k, false);
    return r;
  }

  bool insert(const char *key, size_t key_sz, const char *value,
              size_t value_sz, unsigned tid, unsigned t)
  {
    T k = *reinterpret_cast<const T *>(key);
    Insert(k, value, false);
    return true;
  }
  bool insertResize(const char *key, size_t key_sz, const char *value,
                    size_t value_sz)
  {
    T k = *reinterpret_cast<const T *>(key);
    return Insert(k, value, false);
  }
  bool update(const char *key, size_t key_sz, const char *value,
              size_t value_sz)
  {
    return true;
  }

  bool remove(const char *key, size_t key_sz, unsigned tid)
  {
    T k = *reinterpret_cast<const T *>(key);
    Delete(k, false);
    return true;
  }

  int scan(const char *key, size_t key_sz, int scan_sz, char *&values_out)
  {
    return scan_sz;
  }
};

#endif // _HASH_INTERFACE_H_
