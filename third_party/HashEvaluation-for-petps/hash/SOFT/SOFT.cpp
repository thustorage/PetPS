#include "SOFT.h"

#include <iostream>
extern "C" hash_api *create_hashtable(const hashtable_options_t &opt, unsigned sz, unsigned tnum)
{
  return new SOFTHashTable<uintptr_t>();
}