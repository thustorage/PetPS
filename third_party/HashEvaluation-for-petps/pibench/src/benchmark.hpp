#ifndef __NVM_TREE_BENCH_HPP__
#define __NVM_TREE_BENCH_HPP__

#include <chrono>  // std::chrono::high_resolution_clock::time_point
#include <cstdint>
#include <memory>  // For unique_ptr
#include <vector>

#include "cpucounters.h"
#include "hash_api.h"
#include "key_generator.hpp"
#include "operation_generator.hpp"
#include "stopwatch.hpp"
#include "value_generator.hpp"
namespace PiBench {

void print_environment();

/**
 * @brief Supported random number distributions.
 *
 */
enum class distribution_t : uint8_t {
  UNIFORM = 0,
  SELFSIMILAR = 1,
  ZIPFIAN = 2
};

/**
 * @brief Benchmark options.
 *
 */
struct options_t {
  bool load_factor = false;
  bool recovery = false;
  bool latency = false;
  bool pm = false;
  bool throughput = false;
  bool bandwidth = false;
  bool negative = 0;
  int samples = 2000000;
  int hash_size = 4096;
  /// Name of tree library file used to run the benchmark against.
  std::string library_file = "";
  std::string test_mode = "THROUGHPUT";
  /// Number of records to insert into tree during 'load' phase.
  uint64_t num_records = 0;

  /// Number of operations to issue during 'run' phase.
  uint64_t num_ops = 1024;

  /// Number of parallel threads used for executing requests.
  uint32_t num_threads = 1;

  /// Sampling window in milliseconds.
  uint32_t sampling_ms = 1000;

  /// Key prefix.
  std::string key_prefix = "";

  /// Size of key in bytes.
  uint32_t key_size = 8;

  /// Size of value in bytes.
  uint32_t value_size = 8;

  /// Ratio of read operations.
  float read_ratio = 0.0;

  /// Ratio of insert operations.
  float insert_ratio = 1.0;

  /// Ratio of update operations.
  float update_ratio = 0.0;

  /// Ratio of remove operations.
  float remove_ratio = 0.0;

  /// Ratio of scan operations.
  float scan_ratio = 0.0;

  /// Ratio of resize operations.
  float resize_ratio = 0.0;
  float negative_ratio = 0.0;
  /// Size of scan operations in records.
  uint32_t scan_size = 100;

  /// Distribution used for generation random keys.
  distribution_t key_distribution = distribution_t::UNIFORM;

  /// Factor to be used for skewed random key distributions.
  float key_skew = 0.2;

  /// Master seed to be used for random generations.
  uint32_t rnd_seed = 1729;

  /// Whether to enable Intel PCM for profiling.
  bool enable_pcm = false;

  /// Whether to skip the load phase.
  bool skip_load = true;

  /// Ratio of requests to sample latency from (between 0.0 and 1.0).
  float latency_sampling = 0.0;
};

/**
 * @brief Statistics collected over time.
 *
 */
struct alignas(64) stats_t {
  stats_t() : operation_count(0) {}

  /// Number of operations completed.
  uint64_t operation_count;

  /// Vector to store both start and end time of requests.
  std::vector<std::chrono::high_resolution_clock::time_point> times;

  /// Padding to enforce cache-line size and avoid cache-line ping-pong.
  uint64_t ____padding[7];
};
class pair {
 public:
  char* key;
  char* value;
  pair() {}
  pair(const char*& k, int key_size, const char*& v, int value_size) {
    key = new char[key_size];
    value = new char[value_size];
    memcpy(key, k, key_size);
    memcpy(value, v, value_size);
  }
};
class benchmark_t {
 public:
  /**
   * @brief Construct a new benchmark_t object.
   *
   * @param tree pointer to tree data structure compliant with the API.
   * @param opt options used to run the benchmark.
   */
  benchmark_t(hash_api* tree, const options_t& opt) noexcept;

  /**
   * @brief Destroy the benchmark_t object.
   *
   */
  ~benchmark_t();

  /**
   * @brief Load the tree with the amount of records specified in options_t.
   *
   * A single thread is used for load phase to guarantee determinism across
   * multiple runs. Using multiple threads can lead to different, but
   * equivalent, results.
   */
  void load() noexcept;
  void run() noexcept;
  void search() noexcept;

  /// Run the workload as specified by options_t.
  void clflushAmence() noexcept;
  void tail_search() noexcept;
  void tail_insert() noexcept;
  void recovery() noexcept;
  /// Maximum number of records to be scanned.
  static constexpr size_t MAX_SCAN = 1000;

 private:
  /// Array to store keys and values.
  pair* kvs;
  /// Tree data structure being benchmarked.
  hash_api* hashtable_;

  /// Options used to run this benchmark.
  const options_t opt_;

  /// Operation generator.
  operation_generator_t op_generator_;

  /// Key generator.
  std::unique_ptr<key_generator_t> key_generator_;

  /// Value generator.
  value_generator_t value_generator_;

  /// Intel PCM handler.
  PCM* pcm_;
};
}  // namespace PiBench

namespace std {
std::ostream& operator<<(std::ostream& os, const PiBench::distribution_t& dist);
std::ostream& operator<<(std::ostream& os, const PiBench::options_t& opt);
}  // namespace std

#endif
