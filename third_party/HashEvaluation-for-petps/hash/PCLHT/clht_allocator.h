#pragma once
#include "third_party/dash/third_party/pmdk/src/PMDK/src/include/libpmemobj.h"

#include <sys/stat.h>

#define CREATE_MODE_RW (S_IWUSR | S_IRUSR)

namespace clht_allocator {

static const char* layout_name = "clht_hashtable";
static const constexpr uint64_t pool_addr = 0x5f0000000000;
static const constexpr uint64_t kPMDK_PADDING = 48;

static bool FileExists(const char* pool_path) {
  struct stat buffer;
  return (stat(pool_path, &buffer) == 0);
}

static void flush(void* addr) {
#if CASCADE_LAKE == 1
  _mm_clwb(addr);
#else
  _mm_clflush(addr);
#endif
}


POBJ_LAYOUT_BEGIN(allocator);
POBJ_LAYOUT_TOID(allocator, char)
POBJ_LAYOUT_END(allocator)

/// The problem is how do you allocate memory for the allocator:
///  1. Use allocator as root object.
///  2. Have a separate pool management.
class Allocator {
 public:
  static void Initialize(const char* pool_name, size_t pool_size) {
    PMEMobjpool* pm_pool{nullptr};
    if (!clht_allocator::FileExists(pool_name)) {
      std::cout << "creating a new pool" << std::endl;
      pm_pool =
          pmemobj_create_addr(pool_name, layout_name, pool_size, CREATE_MODE_RW, (void*)pool_addr);
      if (pm_pool == nullptr) {
        std::cout << "failed to create a pool" << std::endl;
      }
    } else {
      std::cout << "opening an existing pool, and trying to map to same address"
                << std::endl;
      pm_pool = pmemobj_open_addr(pool_name, layout_name, (void*)pool_addr);
      if (pm_pool == nullptr) {
        std::cout << "failed to open the pool" << std::endl;
      }
    }
    auto allocator_oid = pmemobj_root(pm_pool, sizeof(Allocator));
    allocator_ = (Allocator*)pmemobj_direct(allocator_oid);
    allocator_->pm_pool_ = pm_pool;
    std::cout << "pool opened at: " << std::hex << allocator_->pm_pool_
              << std::dec << std::endl;
    clht_allocator::flush(allocator_->pm_pool_);
  }

  /// PMDK allocator will add 16-byte meta to each allocated memory, which
  /// breaks the padding, we fix it by adding 48-byte more.
  static void Allocate(void** addr, size_t size) {
    PMEMoid ptr;
    int ret = pmemobj_zalloc(allocator_->pm_pool_, &ptr,
                             sizeof(char) * (size + kPMDK_PADDING),
                             TOID_TYPE_NUM(char));
    if (ret) {
      std::cout << "POBJ_ALLOC error" << std::endl;
    }
    *addr = (char*)pmemobj_direct(ptr) + kPMDK_PADDING;
  }

  static void Free(void* addr) {
    auto addr_oid = pmemobj_oid(addr);
    pmemobj_free(&addr_oid);
  }

  static void* GetRoot(size_t size) {
    return pmemobj_direct(pmemobj_root(allocator_->pm_pool_, size));
  }

 private:
  static Allocator* allocator_;
  PMEMobjpool* pm_pool_{nullptr};
};

Allocator* Allocator::allocator_{nullptr};

}  // namespace clht_allocator