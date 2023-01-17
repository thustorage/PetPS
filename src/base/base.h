#pragma once
#include "folly/Format.h"
#include "folly/Random.h"

#include <cstdint>

#ifndef __has_include
static_assert(false, "__has_include not supported");
#else
#if __cplusplus >= 201703L && __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#elif __has_include(<boost/filesystem.hpp>)
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#endif
#endif

#include <iostream>
#include <thread>

#if defined(__GNUC__)
#define LIKELY(x) (__builtin_expect((x), 1))
#define UNLIKELY(x) (__builtin_expect((x), 0))
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

#include <chrono>
#include <cstdint>
#include <mutex>

using uint8 = uint8_t;
using int8 = int8_t;
using int64 = int64_t;
using int32 = int32_t;
using uint64 = uint64_t;
using uint32 = uint32_t;

#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
 private:                                  \
  TypeName(const TypeName &);              \
  void operator=(const TypeName &)
#endif

namespace base {

// Get microseconds sine Epoch (1970-01-01 00:00:00 000000)
// NOTE: it's micro-seconds (0.000001 second), not milli-seconds (0.001 second).
inline int64 GetTimestamp() {
  auto p1 = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(
             p1.time_since_epoch())
      .count();
}

class Lock {
 public:
  void lock() { m.lock(); }
  void unLock() { m.unlock(); }

 private:
  std::mutex m;
};

class AutoLock {
 public:
  AutoLock(Lock &lock) {
    lock_ = &lock;
    lock_->lock();
  }

  ~AutoLock() { lock_->unLock(); }

 private:
  Lock *lock_;
};

inline std::string IntToString(int x) { return std::to_string(x); }
inline std::string Int64ToString(int64 x) { return std::to_string(x); }

class file_util {
 public:
  static bool Delete(const std::string &path, bool recursive) {
    if (!PathExists(path)) return false;
    if (recursive) {
      fs::remove_all(path);
    } else {
      fs::remove(path);
    }
    return true;
  }

  static bool PathExists(const std::string &path) { return fs::exists(path); }

  static bool CreateDirectory(const std::string &path) {
    return fs::create_directories(path);
    ;
  }
};

class PseudoRandom {
  folly::Random::DefaultGenerator rng;

 public:
  explicit PseudoRandom(uint64 seed) { rng.seed(seed); }

  PseudoRandom() { rng.seed(base::GetTimestamp()); }

  void SetSeed(uint64 seed) { rng.seed(seed); }

  int GetInt(int min, int max) { return folly::Random::rand32(min, max, rng); }

  uint64 GetUint64() { return folly::Random::rand64(rng); }

  uint64 GetUint64LT(uint64 max) { return folly::Random::rand64(max, rng); }

  // Fills a string of length |length| with with random data and returns it.
  //
  // Not that this is a variation of |RandBytes| with a different return type.
  std::string GetString(size_t length) {
    std::string ret(length, 'a');
    for (int i = 0; i < length; i++) {
      ret[i] = GetInt(1, 256);
    }
    return ret;
  }
};

class Rdtsc {
  static constexpr int CPU_FREQ_MHZ = 3300;

 public:
  static inline void CPUPause(void) { __asm__ volatile("pause" ::: "memory"); }

  static inline void SleepNS(uint64_t ns) {
    if (ns == 0) return;
    unsigned long etcs = ReadTSC() + (unsigned long)(ns * CPU_FREQ_MHZ / 1000);
    while (ReadTSC() < etcs) CPUPause();
  }

  static inline unsigned long ReadTSC(void) {
    unsigned long var;
    unsigned int hi, lo;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    var = ((unsigned long long int)hi << 32) | lo;
    return var;
  }
};

class ScopedTempDir {
 public:
  class FilePath {
    std::string path_;

   public:
    FilePath() {}

    bool exists() const { return fs::exists(path_); }

    void setPath(const std::string &path) { path_ = path; }
    std::string value() const { return path_; }
  };

  ScopedTempDir() {}
  ~ScopedTempDir() { file_util::Delete(path_.value(), true); }

  FilePath path() const { return path_; }

  bool CreateUniqueTempDir() {
    auto temp = folly::Random::rand64();
    auto path = folly::sformat("/tmp/temp-{}", temp);
    file_util::CreateDirectory(path);
    path_.setPath(path);
    return true;
  }

  bool CreateUniqueTempDirUnderPath(const std::string &dir) {
    auto temp = folly::Random::rand64();
    auto path = folly::sformat("{}/temp-{}", dir, temp);
    while (file_util::PathExists(path)) {
      temp = folly::Random::rand64();
      path = folly::sformat("{}/temp-{}", dir, temp);
    }
    auto r = file_util::CreateDirectory(path);
    path_.setPath(path);
    std::cout << path << "-------" << r << std::endl;
    return r;
  }

 private:
  FilePath path_;
};

}  // namespace base
const int32 kInt32Max = static_cast<int32>(0x7FFFFFFF);
