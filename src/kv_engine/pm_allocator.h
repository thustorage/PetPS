#pragma once
#include "third_party/dash/third_party/pmdk/src/PMDK/src/include/libpmemobj.h"

#include "memory/shm_file.h"
#include <atomic>
#include <sys/stat.h>
#include <x86intrin.h>

#define CREATE_MODE_RW (S_IWUSR | S_IRUSR)

namespace pm_allocator {

static bool FileExists(const char *pool_path) {
  struct stat buffer;
  return (stat(pool_path, &buffer) == 0);
}

static const char *layout_name = "cceh_hashtable";
static const constexpr uint64_t pool_addr = 0x600000000000;
static const constexpr uint64_t kPMDK_PADDING = 48;

#if 1

static void flush(void *addr) {
#if CASCADE_LAKE == 1
  _mm_clwb(addr);
#else
  _mm_clflush(addr);
#endif
}

static size_t mem;

POBJ_LAYOUT_BEGIN(allocator);
POBJ_LAYOUT_TOID(allocator, char)
POBJ_LAYOUT_END(allocator)

/// The problem is how do you allocate memory for the allocator:
///  1. Use allocator as root object.
///  2. Have a separate pool management.
class Allocator {
public:
  static void Initialize(const char *pool_name, size_t pool_size) {
    PMEMobjpool *pm_pool{nullptr};
    if (!pm_allocator::FileExists(pool_name)) {
      std::cout << "creating a new pool" << std::endl;
      pm_pool = pmemobj_create_addr(pool_name, layout_name, pool_size,
                                    CREATE_MODE_RW, (void *)pool_addr);
      if (pm_pool == nullptr) {
        std::cout << "failed to create a pool" << std::endl;
      }
    } else {
      std::cout
          << "opening an existing pool, and trying to map to same address "
          << std::endl;
      pm_pool = pmemobj_open_addr(pool_name, layout_name, (void *)pool_addr);
      if (pm_pool == nullptr) {
        std::cout << "failed to open the pool" << std::endl;
      }
    }
    auto allocator_oid = pmemobj_root(pm_pool, sizeof(Allocator));
    allocator_ = (Allocator *)pmemobj_direct(allocator_oid);
    allocator_->pm_pool_ = pm_pool;
    std::cout << "pool opened at: " << std::hex << allocator_->pm_pool_
              << std::dec << std::endl;
    pm_allocator::flush(allocator_->pm_pool_);

    mem = 0;
  }

  /// PMDK allocator will add 16-byte meta to each allocated memory, which
  /// breaks the padding, we fix it by adding 48-byte more.
  static void Allocate(void **addr, size_t size) {
    PMEMoid ptr;
    int ret = pmemobj_zalloc(allocator_->pm_pool_, &ptr,
                             sizeof(char) * (size + kPMDK_PADDING),
                             TOID_TYPE_NUM(char));
    if (ret) {
      std::cout << "POBJ_ALLOC error" << std::endl;
    }
    mem += sizeof(char) * (size + kPMDK_PADDING);
    // std::cout << mem << std::endl;
    *addr = (char *)pmemobj_direct(ptr) + kPMDK_PADDING;
  }

  static void Free(void *addr) {
    auto addr_oid = pmemobj_oid(addr);
    pmemobj_free(&addr_oid);
  }

  static void *GetRoot(size_t size) {
    return pmemobj_direct(pmemobj_root(allocator_->pm_pool_, size));
  }

  static void PrintMem() {
    printf("memory usage: %d\n", (int)mem);
    sleep(10);
  }

private:
  static Allocator *allocator_;
  PMEMobjpool *pm_pool_{nullptr};
};

Allocator *Allocator::allocator_{nullptr};

#else
class Allocator {

public:
  static constexpr int64_t align = 2 * 1024 * 1024LL;
  static void Initialize(const char *pool_name, size_t pool_size) {
    CHECK(instance_ == nullptr);
    instance_ = new Allocator(pool_name, pool_size);
  }

  static void Allocate(void **addr, size_t size) {
    // constexpr int64_t align = 4 * 1024LL;
    // constexpr int64_t align = 64;
    auto new_size = (size + align - 1) / align * align;
    *addr = instance_->start_ + instance_->offset_.fetch_add(new_size);
    CHECK((uint64_t)(instance_->start_) % 64 == 0);
    CHECK((uint64_t)(*addr) % 64 == 0);
  }
  static void PrintMem() {
    printf("memory usage: %d\n", (int)instance_->offset_);
  }

private:
  Allocator(const char *pool_name, size_t pool_size) : pool_name_(pool_name) {
    shmfile_ = new base::ShmFile();
    if (!shmfile_->Initialize(pool_name_, pool_size)) {
      base::file_util::Delete(pool_name_, false);
      CHECK(shmfile_->Initialize(pool_name_, pool_size));
      LOG(INFO) << "Reinitialize shm dict size: " << pool_size;
    }
    start_ = shmfile_->Data();
    uint64_t start_num = (uint64_t)start_;
    start_ = (char *)((start_num + align - 1) / align * align);
    CHECK((uint64_t)start_ % 64 == 0);
  }

private:
  static Allocator *instance_;
  base::ShmFile *shmfile_;
  char *start_;
  std::atomic<uint64_t> offset_{0};
  std::string pool_name_;
};

Allocator *Allocator::instance_;
#endif

template <typename T> class PMAllocator {
public:
  using value_type = T;
  using pointer = T *;
  using const_pointer = const T *;

  using void_pointer = void *;
  using const_void_pointer = const void *;

  using size_type = size_t;
  using difference = std::ptrdiff_t;

  pointer allocate(size_type numObjs) {
    void *ptr;
    Allocator::Allocate(&ptr, sizeof(T) * numObjs);
    return static_cast<pointer>(ptr);
  }

  pointer allocate(size_type numObjs, const_void_pointer hit) {
    return allocate(numObjs);
  }

  constexpr void deallocate(pointer p, size_type numObjs) { return; }

  template <class U> using rebind = PMAllocator<U>;

  PMAllocator() = default;
  PMAllocator(const PMAllocator &aOther) = default;
  template <class O> PMAllocator(const PMAllocator<O> &aOther){};
  ~PMAllocator() = default;
};

} // namespace pm_allocator