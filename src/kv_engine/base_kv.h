#pragma once
#include "base/array.h"
#include <string>
#include <tuple>

#include "third_party/HashEvaluation-for-petps/hash/common/hash_api.h"

#define XMH_SIMPLE_MALLOC

struct BaseKVConfig {
  int value_size;
  int64_t hash_size;
  size_t pool_size;
  int num_threads;
  int64_t capacity;
  std::string path;
  std::string library_file;
  std::string hash_name = "clht";
};

class BaseKV {
public:
  virtual ~BaseKV() {std::cout << "exit BaseKV" << std::endl;}
  explicit BaseKV(const BaseKVConfig &config){};
  virtual void Util() { std::cout << "BaseKV Util: no impl" << std::endl; return; }
  virtual void Get(const uint64_t key, std::string &value, unsigned tid) = 0;
  virtual void Put(const uint64_t key, const std::string_view &value, unsigned tid) = 0;
  virtual void BatchGet(base::ConstArray<uint64_t> keys,
                        std::vector<base::ConstArray<float>> *values, unsigned tid) = 0;

  virtual std::pair<uint64_t, uint64_t> RegisterPMAddr() const = 0;

  virtual void DebugInfo() const {};
};
