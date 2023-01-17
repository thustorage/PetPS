#include "benchmark.hpp"

#include <omp.h>
#include <sys/utsname.h>  // uname

#include <algorithm>
#include <cassert>
#include <cmath>  // std::ceil
#include <ctime>
#include <fstream>
#include <functional>  // std::bind
#include <iostream>
#include <map>
#include <regex>  // std::regex_replace
#include <string>
#include <vector>

#include "hash_api.h"
#include "utils.hpp"
using namespace std;
namespace PiBench {
void print_environment() {
  std::time_t now = std::time(nullptr);
  uint64_t num_cpus = 0;
  std::string cpu_type;
  std::string cache_size;

  std::ifstream cpuinfo("/proc/cpuinfo", std::ifstream::in);

  if (!cpuinfo.good()) {
    num_cpus = 0;
    cpu_type = "Could not open /proc/cpuinfo";
    cache_size = "Could not open /proc/cpuinfo";
  } else {
    std::string line;
    while (!getline(cpuinfo, line).eof()) {
      auto sep_pos = line.find(':');
      if (sep_pos == std::string::npos) {
        continue;
      } else {
        std::string key = std::regex_replace(std::string(line, 0, sep_pos),
                                             std::regex("\\t+$"), "");
        std::string val = sep_pos == line.size() - 1
                              ? ""
                              : std::string(line, sep_pos + 2, line.size());
        if (key.compare("model name") == 0) {
          ++num_cpus;
          cpu_type = val;
        } else if (key.compare("cache size") == 0) {
          cache_size = val;
        }
      }
    }
  }
  cpuinfo.close();

  std::string kernel_version;
  struct utsname uname_buf;
  if (uname(&uname_buf) == -1) {
    kernel_version = "Unknown";
  } else {
    kernel_version =
        std::string(uname_buf.sysname) + " " + std::string(uname_buf.release);
  }

  std::cout << "Environment:"
            << "\n"
            << "\tTime: " << std::asctime(std::localtime(&now))
            << "\tCPU: " << num_cpus << " * " << cpu_type << "\n"
            << "\tCPU Cache: " << cache_size << "\n"
            << "\tKernel: " << kernel_version << std::endl;
}

benchmark_t::benchmark_t(hash_api *hashtable, const options_t &opt) noexcept
    : hashtable_(hashtable),
      opt_(opt),
      op_generator_(opt.read_ratio, opt.insert_ratio, opt.remove_ratio),
      value_generator_(opt.value_size),
      pcm_(nullptr) {
  if (opt.enable_pcm) {
    pcm_ = PCM::getInstance();
    auto status = pcm_->program();
    if (status != PCM::Success) {
      std::cout << "Error opening PCM: " << status << std::endl;
      if (status == PCM::PMUBusy)
        pcm_->resetPMU();
      else
        exit(0);
    }
  }

  size_t key_space_sz = opt_.num_ops;
  switch (opt_.key_distribution) {
    case distribution_t::UNIFORM:
      key_generator_ = std::make_unique<uniform_key_generator_t>(
          key_space_sz, opt_.key_size, opt_.key_prefix);
      break;

    case distribution_t::SELFSIMILAR:
      key_generator_ = std::make_unique<selfsimilar_key_generator_t>(
          key_space_sz, opt_.key_size, opt_.key_prefix, opt_.key_skew);
      break;

    case distribution_t::ZIPFIAN:
      key_generator_ = std::make_unique<zipfian_key_generator_t>(
          key_space_sz, opt_.key_size, opt_.key_prefix, opt_.key_skew);
      break;

    default:
      std::cout << "Error: unknown distribution!" << std::endl;
      exit(0);
  }
}

benchmark_t::~benchmark_t() {
  if (pcm_) pcm_->cleanup();
  delete[] kvs;
}

void benchmark_t::load() noexcept {
  std::cout << std::fixed << std::setprecision(1);
  std::cout << "loading..." << std::endl;
  kvs = new pair[opt_.num_ops];
  auto kvs1 = new pair[opt_.num_ops];
  stopwatch_t sw;
  sw.start();
  auto negative_size = opt_.negative_ratio * opt_.num_ops;
  map<uint64_t, uint64_t> cf;
  for (uint64_t i = 0; i < opt_.num_ops; ++i) {
    auto key_ptr = key_generator_->next(false);
    // Generate random value
    auto value_ptr = value_generator_.next();
    kvs[i] = pair(key_ptr, opt_.key_size, value_ptr, opt_.value_size);
    auto key_ptr1 = key_generator_->next(true);
    kvs1[i] = pair(key_ptr1, opt_.key_size, value_ptr, opt_.value_size);
  }
#pragma omp parallel num_threads(opt_.num_threads)
  {
    auto tid = omp_get_thread_num();
    uint64_t counter = 0;
    hashtable_->thread_ini(tid);
#pragma omp for schedule(static)
    for (uint64_t i = 0; i < opt_.num_ops; ++i) {
      if (!opt_.skip_load) {
        if (i < negative_size) {
          hashtable_->insert(kvs1[i].key, opt_.key_size, kvs1[i].value,
                        opt_.value_size, tid, counter);
        } else {
          hashtable_->insert(kvs[i].key, opt_.key_size, kvs[i].value,
                        opt_.value_size, tid, counter);
        }
      }
    }
  }
  delete[] kvs1;
  auto elapsed = sw.elapsed<std::chrono::milliseconds>();
  std::cout << "\nOverview:"
            << "\n"
            << "\tLoad time: " << elapsed << " milliseconds" << std::endl;
}
void benchmark_t::run() noexcept {
  std::cout << "runing..." << std::endl;
  stopwatch_t swt;
  uint64_t current_id = key_generator_->current_id_;
  float elapsed;
  uint64 total_resize_time = 0;
  uint64_t ttt = 0;
  uint64 sample_stride = opt_.num_ops / opt_.samples;
  uint64 load_factor_stride = opt_.num_ops / 200;
  uint64 last_sample = -sample_stride;
  std::vector<uint64> latency[opt_.num_threads];
  if (opt_.latency)
    for (auto &&i : latency) {
      i.reserve(opt_.samples);
    }
  std::unique_ptr<SystemCounterState> before_sstate;
  if (opt_.pm) {
    before_sstate = std::make_unique<SystemCounterState>();
    *before_sstate = getSystemCounterState();
  }
#pragma omp parallel num_threads(opt_.num_threads)
  {
    auto tid = omp_get_thread_num();
    uint64_t counter = 0;
    stopwatch_t sw;
    stopwatch_t swl;
    char value_out[value_generator_t::VALUE_MAX];
#pragma omp barrier

#pragma omp single nowait
    { swt.start(); }

#pragma omp for schedule(static)
    for (uint64_t i = 0; i < opt_.num_ops; ++i) {
      // Generate random operation
      auto op = op_generator_.next();
      auto key_ptr = kvs[i].key;
      auto value_ptr = kvs[i].value;
      if (opt_.latency) {
        swl.start();
      }
      switch (op) {
        case operation_t::READ: {
          hashtable_->find(key_ptr, opt_.key_size, value_out, tid);
          break;
        }

        case operation_t::INSERT: {
          ////for insert
          if (!opt_.resize_ratio) {
            hashtable_->insert(key_ptr, opt_.key_size, value_ptr, opt_.value_size,
                          tid, counter++);
          }
          //// for resizing
          else if (opt_.resize_ratio) {
            sw.start();
            auto r = hashtable_->insertResize(key_ptr, opt_.key_size, value_ptr,
                                         opt_.value_size, tid, counter++);

            auto ela = sw.elapsed<std::chrono::nanoseconds>();
            std::cout << ela << std::endl;
            total_resize_time += ela;
          }

          // // for load_factor
          if (opt_.load_factor && i % load_factor_stride == 0) {
            auto u = hashtable_->utilization();
            cout << "Inserted: " << i / 1000000
                 << "M load factor: " << u.load_factor
                 << "  utilization: " << u.utilization << "\n";
          }

          break;
        }
        case operation_t::REMOVE: {
          auto r = hashtable_->remove(key_ptr, opt_.key_size, tid);
          break;
        }
        default:
          std::cout << "Error: unknown operation!" << std::endl;
          exit(0);
          break;
      }
      if (opt_.latency && i % sample_stride == 0) {
        auto t = swl.elapsed<std::chrono::nanoseconds>();
        latency[tid].push_back(t);
      }
    }
  }
#pragma omp single nowait
  { elapsed = swt.elapsed<std::chrono::microseconds>(); }
  if (opt_.pm) {
    std::unique_ptr<SystemCounterState> after_sstate;
    after_sstate = std::make_unique<SystemCounterState>();
    *after_sstate = getSystemCounterState();
    std::cout << "PM:\n"
              << "\tReadBytes per second: "
              << (float)getBytesReadFromPMM(*before_sstate, *after_sstate) /
                     (elapsed / 1000)
              << "\n"
              << "\tWriteBytes per second: "
              << (float)getBytesWrittenToPMM(*before_sstate, *after_sstate) /
                     (elapsed / 1000)
              << "\n"
              << "\tReadBytes total: "
              << (float)getBytesReadFromPMM(*before_sstate, *after_sstate)
              << "\n"
              << "\tWriteBytes total: "
              << (float)getBytesWrittenToPMM(*before_sstate, *after_sstate)
              << "\n"
              << "\tNVM Reads (bytes) per operation: "
              << (float)getBytesReadFromPMM(*before_sstate, *after_sstate) /
                     opt_.num_ops
              << "\n"
              << "\tNVM Writes (bytes) per operation: "
              << (float)getBytesWrittenToPMM(*before_sstate, *after_sstate) /
                     opt_.num_ops
              << "\n"
              << "\tLL3 misses: "
              << getL3CacheMisses(*before_sstate, *after_sstate) << "\n"
              << "\tRun time(ms): " << elapsed / 1000 << "\n"
              << "\tThroughput(Mops/s): " << (float)opt_.num_ops / elapsed
              << std::endl;
  }
  if (opt_.throughput) {
    std::cout << "\tRun time(ms): " << elapsed / 1000 << std::endl;
    std::cout << "\tThroughput(Mops/s): " << (float)opt_.num_ops / elapsed
              << std::endl;
  }
  if (opt_.latency) {
    std::cout << "\tLatency(ns): \t" << std::endl;
    auto v = std::vector<uint64>();
    for (auto &&i : latency) {
      v.insert(v.end(), i.begin(), i.end());
    }
    sort(v.begin(), v.end());
    int sz = v.size();
    cout << "0 " << v[0] << "\n"
         << "1 " << v[0.5 * sz] << "\n"
         << "2 " << v[0.9 * sz] << "\n"
         << "3 " << v[0.99 * sz] << "\n"
         << "4 " << v[0.999 * sz] << "\n"
         << "5 " << v[0.9999 * sz] << "\n"
         << "6 " << v[0.99999 * sz] << std::endl;
  }
  if (opt_.resize_ratio) {
    cout << total_resize_time << " " << elapsed << std::endl;
  }
}
}  // namespace PiBench

namespace std {
std::ostream &operator<<(std::ostream &os,
                         const PiBench::distribution_t &dist) {
  switch (dist) {
    case PiBench::distribution_t::UNIFORM:
      return os << "UNIFORM";
      break;
    case PiBench::distribution_t::SELFSIMILAR:
      return os << "SELFSIMILAR";
      break;
    case PiBench::distribution_t::ZIPFIAN:
      return os << "ZIPFIAN";
      break;
    default:
      return os << static_cast<uint8_t>(dist);
  }
}

std::ostream &operator<<(std::ostream &os, const PiBench::options_t &opt) {
  os << "Benchmark Options:"
     << "\n"
     << "\tTarget: " << opt.library_file << "\n"
     << "\t# Operations: " << opt.num_ops << "\n"
     << "\t# Threads: " << opt.num_threads << "\n"
     << "\tKey size: " << opt.key_size << "\n"
     << "\tValue size: " << opt.value_size << "\n"
     << "\tRandom seed: " << opt.rnd_seed << "\n"
     << "\tKey distribution: " << opt.key_distribution
     << (opt.key_distribution == PiBench::distribution_t::SELFSIMILAR ||
                 opt.key_distribution == PiBench::distribution_t::ZIPFIAN
             ? "(" + std::to_string(opt.key_skew) + ")"
             : "")
     << "\n"
     << "\tOperations ratio:\n"
     << "\t\tRead: " << opt.read_ratio << "\n"
     << "\t\tInsert: " << opt.insert_ratio << "\n"
     << "\t\tUpdate: " << opt.update_ratio << "\n"
     << "\t\tDelete: " << opt.remove_ratio << "\n";
  return os;
}
}  // namespace std