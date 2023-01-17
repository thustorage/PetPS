#pragma once

#include <fcntl.h>
#include <sys/mman.h>
#include <atomic>
#include <deque>
#include <fstream>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "base/base.h"
#include "pet_kv/persistence.h"

#include <folly/GLog.h>
#include <folly/String.h>

DECLARE_bool(use_dram);

namespace base {

class PMMmapRegisterCenter {
  struct MetaBlock {
    static const int MM_META_NR = 1000;

    MetaBlock() {
      mmMetaNr_ = 0;
      magic_ = 0xdeadbeef;
      for (int i = 0; i < MetaBlock::MM_META_NR; i++)
        mmMeta_[i].magic_ = 0x66666666;
    }
    struct MmMeta {
      MmMeta() {}
      char file_name_[64];
      uint64_t offset_;
      uint64_t size_;
      int magic_ = 0x66666666;
    };
    union {
      struct {
        int magic_ = 0xdeadbeef;
        int mmMetaNr_ = 0;
        MmMeta mmMeta_[MM_META_NR];
      };
      char _[2 * 4096 * 4096LL];
    };
  };

 public:
  static PMMmapRegisterCenter *GetInstance() {
    static PMMmapRegisterCenter instance;
    return &instance;
  }
  static constexpr int64_t align = 2 * 1024 * 1024LL;
  // static constexpr int64_t align = 4 * 1024LL;

  void *Register(const std::string &name, uint64_t size) {
    std::lock_guard<std::mutex> _(mutex_);
    auto *item = FindIfExists(name);
    if (item == nullptr) {
      return append_register(name, size);
    } else if (item->size_ != size) {
      // reinit MMAP
      LOG(INFO) << folly::sformat("PM mmap {} registered size{} != existing{}",
                                  name, size, item->size_);
      ReInitialize();
      return append_register(name, size);
    } else {
      LOG(INFO) << folly::sformat("PM mmap {} use existing map", name);

      shmfile_mmap_addr_ =
          std::max(shmfile_mmap_addr_.load(),
                   shmfile_mmap_data_addr_start_ + item->offset_ + item->size_);
      return (void *)(shmfile_mmap_data_addr_start_ + item->offset_);
    }
  }

  std::pair<uint64_t, uint64_t> ForRDMAMemoryRegion() const {
    return std::make_pair(
        shmfile_mmap_data_addr_start_,
        shmfile_mmap_addr_.load() - shmfile_mmap_data_addr_start_);
  }
  // this function is non-reentrant
  void ReInitialize() {
    LOG(WARNING) << "reinitialize the whole PM space";
    static std::atomic_int flag{false};
    if (!flag.fetch_or(true)) {
      LOG(WARNING) << "memset whole meta block";
      memset((void *)metaBlock_, 0, sizeof(MetaBlock));
      new (metaBlock_) MetaBlock();
    } else {
      LOG(FATAL) << "ReInitialize is non-reentrant";
    }
    CHECK(Valid());
  }

  struct Config {
    bool use_dram = false;
    int numa_id = 0;
  };

  static Config &GetConfig() {
    static Config config;
    return config;
  }

 private:
  bool Valid() const {
    if (metaBlock_->magic_ != 0xdeadbeef) return false;
    for (int i = 0; i < MetaBlock::MM_META_NR; i++)
      if (metaBlock_->mmMeta_[i].magic_ != 0x66666666) return false;
    return true;
  }

  void *append_register(const std::string &name, uint64_t size) {
    CHECK_NE(size, 0);
    // LOG(INFO) << fmt::format("PM mmap register {}, size={}", name, size);
    auto new_size = (size + align - 1) / align * align;
    auto ret = shmfile_mmap_addr_.fetch_add(new_size);
    LOG(ERROR) << "xmh "
               << (shmfile_mmap_addr_ - shmfile_mmap_addr_start_) /
                      (1024 * 1024 * 1024LL);
    CHECK_GE((uint64_t)ret, shmfile_mmap_data_addr_start_);
    CHECK_LT((uint64_t)ret + size,
             shmfile_mmap_data_addr_start_ + dax_mm_size_);
    memset((void *)ret, 0, size);

    // persist meta data
    auto &item = metaBlock_->mmMeta_[metaBlock_->mmMetaNr_];
    std::strncpy(item.file_name_, name.c_str(), 50);
    item.offset_ = ret - shmfile_mmap_data_addr_start_;
    item.size_ = size;
    metaBlock_->mmMetaNr_++;
    CHECK_LE(metaBlock_->mmMetaNr_, MetaBlock::MM_META_NR);
    base::clflushopt_range(metaBlock_, sizeof(MetaBlock));
    return (char *)ret;
  }

  MetaBlock::MmMeta *FindIfExists(const std::string &name) {
    for (int i = 0; i < metaBlock_->mmMetaNr_; i++) {
      auto &item = metaBlock_->mmMeta_[i];
      if (std::strncmp(item.file_name_, name.c_str(), 64) == 0) {
        CHECK_EQ(item.magic_, 0x66666666);
        return &item;
      }
    }
    return nullptr;
  }

  PMMmapRegisterCenter() {
    CHECK_NE(GetConfig().numa_id, -1);

    char *data;
    if (GetConfig().use_dram) {
      // filename_ = folly::sformat("/dev/shm/big_file{}", GetConfig().numa_id);
      fd_ = 0;
      LOG(WARNING) << "use dram mmap for PMMmapRegisterCenter";
      data = reinterpret_cast<char *>(
          mmap(nullptr, dax_mm_size_, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_POPULATE | MAP_ANONYMOUS, -1, 0));
      metaBlock_ = (MetaBlock *)data;
      CHECK_NE(data, MAP_FAILED) << "map failed";
      mlock(data, dax_mm_size_);
      ReInitialize();
    } else {
      filename_ = folly::sformat("/dev/dax{}.0", GetConfig().numa_id);
      fd_ = open(filename_.c_str(), 0666);
      if (fd_ < 0) {
        LOG(FATAL) << "Failed to open file " << filename_ << ": "
                   << strerror(errno);
      }
      data = reinterpret_cast<char *>(mmap(nullptr, dax_mm_size_,
                                           PROT_READ | PROT_WRITE,
                                           MAP_SHARED | MAP_POPULATE, fd_, 0));
    }

    LOG(INFO) << "mmap " << filename_ << " ; size = " << dax_mm_size_;
    CHECK_NE(data, MAP_FAILED) << "map failed";
    // read meta file
    metaBlock_ = (MetaBlock *)data;
    if (!Valid()) {
      //  first use this device
      LOG(ERROR)
          << "metadata of dax is not valid, maybe you first use this device;"
          << " Please use pm_command to reinit";
      // ReInitialize();
    }

    *(uint64_t *)&shmfile_mmap_addr_start_ = (uint64_t)data;
    *(uint64_t *)&shmfile_mmap_data_addr_start_ =
        (uint64_t)data + sizeof(MetaBlock);
    shmfile_mmap_addr_.store(shmfile_mmap_data_addr_start_);
  }
  ~PMMmapRegisterCenter() {
    if (fd_ >= 0) {
      LOG(INFO) << "ummap shm file: " << filename_ << ", size: " << dax_mm_size_
                << ", fd: " << fd_;
      munmap((void *)shmfile_mmap_addr_start_, dax_mm_size_);
      close(fd_);
      fd_ = -1;
    }
  }

  std::string filename_;
  // const int64_t dax_mm_size_ = 300 * 1024 * 1024 * 1024LL;
  const int64_t dax_mm_size_ = 140 * 1024 * 1024 * 1024LL;
  // const int64_t dax_mm_size_ = 500 * 1024 * 1024 * 1024LL;
  // const int64_t dax_mm_size_ = 75 * 1024 * 1024 * 1024LL;
  int fd_;
  const uint64_t shmfile_mmap_addr_start_ = 0;
  const uint64_t shmfile_mmap_data_addr_start_ = 0;
  // const uint64_t shmfile_mmap_addr_start_ = 0xeff4200000ull;
  std::atomic<uint64_t> shmfile_mmap_addr_;

  std::mutex mutex_;

  MetaBlock *metaBlock_ = nullptr;
};

class ShmFile {
 public:
  ShmFile() : data_(NULL), size_(0), fd_(-1) {}
  ~ShmFile() { Clear(); }
  bool Initialize(const std::string &filename, int64 size);
  char *Data() const { return data_; }
  int64 Size() const { return size_; }
  const std::string &filename() const { return filename_; }

 private:
  void Clear();

  bool InitializeDevDax(const std::string &filename, int64 size);
  bool InitializeFsDax(const std::string &filename, int64 size);
  void ClearDevDax();
  void ClearFsDax();

  std::string filename_;
  char *data_;
  int64 size_;
  int fd_;

  DISALLOW_COPY_AND_ASSIGN(ShmFile);
};

}  // namespace base
