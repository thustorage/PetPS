#include "lh_finger.h"
static const char* pool_name = PMEM_LOC "/pmem_hash.data";
// pool size
static const size_t pool_size = 64UL * 1024 * 1024 * 1024;
extern "C" hash_api* create_hashtable(const hashtable_options_t& opt, unsigned sz = 0) {
  // Step 1: create (if not exist) and open the pool
  bool file_exist = false;
  if (FileExists(pool_name)) file_exist = true;
  Allocator::Initialize(pool_name, pool_size);

  // Step 2: Allocate the initial space for the hash table on PM and get the
  // root; we use Dash-EH in this case.
  Hash<uint64_t>* hash_table = reinterpret_cast<Hash<uint64_t>*>(
      Allocator::GetRoot(sizeof(linear::Linear<uint64_t>)));
  // Step 3: Initialize the hash table
  if (!file_exist) {
    new (hash_table) linear::Linear<uint64_t>(Allocator::Get()->pm_pool_);
  } else {
    new (hash_table) linear::Linear<uint64_t>();
  }
  return hash_table;
}