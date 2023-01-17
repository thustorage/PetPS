#include <dlfcn.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <iostream>

#include "benchmark.hpp"
#include "cxxopts.hpp"
#include "hash_api.h"
#include "library_loader.hpp"

using namespace PiBench;

int main(int argc, char **argv) {
  // Parse command line arguments
  options_t opt;
  hashtable_options_t hashtable_opt;
  try {
    cxxopts::Options options("PiBench",
                             "Benchmark framework for persistent indexes.");
    options.positional_help("INPUT").show_positional_help();

    options.add_options()("input", "Absolute path to library file",
                          cxxopts::value<std::string>())(
        "n,records", "Number of records to load",
        cxxopts::value<uint64_t>()->default_value(std::to_string(
            opt.num_records)))("N,negative_ratio", "Negative search ratio",
                               cxxopts::value<float>()->default_value(
                                   std::to_string(opt.negative_ratio)))(
        "S,hash_size", "hashtable size",
        cxxopts::value<int>()->default_value(std::to_string(opt.hash_size)))(
        "M,test_mode", "Test mode",
        cxxopts::value<std::string>()->default_value(opt.test_mode))(
        "p,operations", "Number of operations to execute",
        cxxopts::value<uint64_t>()->default_value(std::to_string(opt.num_ops)))(
        "t,threads", "Number of threads to use",
        cxxopts::value<uint32_t>()->default_value(
            std::to_string(opt.num_threads)))(
        "r,read_ratio", "Ratio of read operations",
        cxxopts::value<float>()->default_value(std::to_string(opt.read_ratio)))(
        "i,insert_ratio", "Ratio of insert operations",
        cxxopts::value<float>()->default_value(std::to_string(
            opt.insert_ratio)))("d,remove_ratio", "Ratio of remove operations",
                                cxxopts::value<float>()->default_value(
                                    std::to_string(opt.remove_ratio)))(
        "skip_load", "Skip the load phase",
        cxxopts::value<bool>()->default_value(
            (opt.skip_load ? "true" : "false")))(
        "distribution", "Key distribution to use",
        cxxopts::value<std::string>()->default_value("UNIFORM"))("help",
                                                                 "Print help");

    options.parse_positional({"input"});

    auto result = options.parse(argc, argv);

    if (result.count("negative_ratio"))
      opt.negative_ratio = result["negative_ratio"].as<float>();

    if (result.count("test_mode"))
      opt.test_mode = result["test_mode"].as<std::string>();

    if (result.count("hash_size"))
      opt.hash_size = result["hash_size"].as<int>();

    if (result.count("help")) {
      std::cout << options.help() << std::endl;
      exit(0);
    }

    if (result.count("pcm")) {
      opt.enable_pcm = result["pcm"].as<bool>();
    }

    if (result.count("skip_load")) {
      opt.skip_load = result["skip_load"].as<bool>();
    }

    if (result.count("latency_sampling")) {
      opt.latency_sampling = result["latency_sampling"].as<float>();
    }

    if (result.count("input")) {
      opt.library_file = result["input"].as<std::string>();
    } else {
      std::cout << "Missing 'input' argument." << std::endl;
      std::cout << options.help() << std::endl;
      exit(0);
    }

    // Parse "num_records"
    if (result.count("records"))
      opt.num_records = result["records"].as<uint64_t>();

    // Parse "num_operations"
    if (result.count("operations"))
      opt.num_ops = result["operations"].as<uint64_t>();

    // Parse "num_threads"
    if (result.count("threads"))
      opt.num_threads = result["threads"].as<uint32_t>();

    // Parse "sampling_ms"
    if (result.count("sampling_ms"))
      opt.sampling_ms = result["sampling_ms"].as<uint32_t>();

    // Parse "key_prefix"
    if (result.count("key_prefix"))
      opt.key_prefix = result["key_prefix"].as<std::string>();

    // Parse "key_size"
    if (result.count("key_size"))
      opt.key_size = result["key_size"].as<uint32_t>();

    // Parse "value_size"
    if (result.count("value_size"))
      opt.value_size = result["value_size"].as<uint32_t>();

    // Parse "ops_ratio"
    if (result.count("read_ratio"))
      opt.read_ratio = result["read_ratio"].as<float>();

    if (result.count("insert_ratio"))
      opt.insert_ratio = result["insert_ratio"].as<float>();

    if (result.count("update_ratio"))
      opt.update_ratio = result["update_ratio"].as<float>();

    if (result.count("remove_ratio"))
      opt.remove_ratio = result["remove_ratio"].as<float>();

    if (result.count("scan_ratio"))
      opt.scan_ratio = result["scan_ratio"].as<float>();

    // Parse 'scan_size'.
    if (result.count("scan_size"))
      opt.scan_size = result["scan_size"].as<uint32_t>();

    // Parse 'key_distribution'
    if (result.count("distribution")) {
      std::string dist = result["distribution"].as<std::string>();
      std::transform(dist.begin(), dist.end(), dist.begin(), ::tolower);
      if (dist.compare("uniform") == 0)
        opt.key_distribution = distribution_t::UNIFORM;
      else if (dist.compare("selfsimilar") == 0)
        opt.key_distribution = distribution_t::SELFSIMILAR;
      else if (dist.compare("zipfian") == 0) {
        std::cout << "WARNING: initializing ZIPFIAN generator might take time."
                  << std::endl;
        opt.key_distribution = distribution_t::ZIPFIAN;
      } else {
        std::cout << "Invalid key distribution, must be one of "
                  << "[UNIFORM | SELFSIMILAR | ZIPFIAN], but is " << dist
                  << std::endl;
        exit(1);
      }
    }

    // Parse 'key_skew'
    if (result.count("skew")) opt.key_skew = result["skew"].as<float>();

    // Parse 'rnd_seed'
    if (result.count("seed")) {
      opt.rnd_seed = result["seed"].as<uint32_t>();
    }

    // Parse "pool_path"
    if (result.count("pool_path"))
      hashtable_opt.pool_path = result["pool_path"].as<std::string>();

    // Parse "pool_size"
    if (result.count("pool_size"))
      hashtable_opt.pool_size = result["pool_size"].as<uint64_t>();
  } catch (const cxxopts::OptionException &e) {
    std::cout << "Error parsing options: " << e.what() << std::endl;
    exit(1);
  }

  // Sanitize options
  if (opt.key_prefix.size() + opt.key_size > key_generator_t::KEY_MAX) {
    std::cout << "Total key size cannot be greater than "
              << key_generator_t::KEY_MAX << ", but is "
              << opt.key_prefix.size() + opt.key_size << std::endl;
    exit(1);
  }

  if (opt.value_size > value_generator_t::VALUE_MAX) {
    std::cout << "Total value size cannot be greater than "
              << value_generator_t::VALUE_MAX << ", but is " << opt.value_size
              << std::endl;
    exit(1);
  }

  auto sum = opt.read_ratio + opt.insert_ratio + opt.update_ratio +
             opt.remove_ratio + opt.scan_ratio + opt.resize_ratio;
  if (sum != 1.0) {
    std::cout << "Sum of ratios should be 1.0 but is " << sum << std::endl;
    exit(1);
  }

  if (opt.scan_size < 1 || opt.scan_size > benchmark_t::MAX_SCAN) {
    std::cout << "Scan size must be in the range [1,"
              << value_generator_t::VALUE_MAX << "], but is " << opt.scan_size
              << std::endl;
    exit(1);
  }

  if (opt.key_distribution == distribution_t::SELFSIMILAR &&
      (opt.key_skew < 0.0 || opt.key_skew > 0.5)) {
    std::cout << "Skew factor must be in the range [0 , 0.5]." << std::endl;
    exit(1);
  }

  if (opt.key_distribution == distribution_t::ZIPFIAN &&
      (opt.key_skew < 0.0 || opt.key_skew > 1.0)) {
    std::cout << "Skew factor must be in the range [0.0 , 1.0]." << std::endl;
    exit(1);
  }

  if ((opt.latency_sampling < 0.0 || opt.latency_sampling > 1.0)) {
    std::cout << "Latency sampling must be in the range [0.0 , 1.0]."
              << std::endl;
    exit(1);
  }
  if (opt.negative_ratio > 0) opt.negative = true;
  if (opt.test_mode == "LOAD_FACTOR") {
    opt.num_threads = 1;
    opt.load_factor = true;
  } else if (opt.test_mode == "RESIZE") {
    opt.resize_ratio = 1;
  } else if (opt.test_mode == "PM") {
    opt.pm = true;
    opt.enable_pcm = true;
  } else if (opt.test_mode == "THROUGHPUT") {
    opt.throughput = true;
  } else if (opt.test_mode == "LATENCY") {
    opt.samples = opt.num_ops / 100;
    opt.latency = true;
  }
  print_environment();
  std::cout << opt << std::endl;

  hashtable_opt.key_size = opt.key_prefix.size() + opt.key_size;
  hashtable_opt.value_size = opt.value_size;
  hashtable_opt.num_threads = opt.num_threads;

  hashtable_opt.pool_path = "/dev/shm//temp-12474327795291262795/dict";
  hashtable_opt.pool_size = 32UL * 1024 * 1024 * 1024;

  library_loader_t lib(opt.library_file);
  std::cerr << "Initializing..." << std::endl;
  hash_api *hashtable = lib.create_hashtable(hashtable_opt, opt.hash_size, opt.num_threads);
  if (hashtable == nullptr) {
    std::cout << "Error instantiating hashtable." << std::endl;
    exit(1);
  }
  // std::cerr << "Successful! " << std::endl;
  // sleep(10);
  benchmark_t bench(hashtable, opt);
  bench.load();
  bench.run();
  return 0;
}
