#include "base/base.h"
#include "third_party/dash/src/Hash.h"
#include "third_party/dash/src/allocator.h"
#include "third_party/dash/src/ex_finger.h"
// #include "third_party/HashEvaluation-for-petps/hash/common/hash_api.h"
#include "/home/xieminhui/HashEvaluation/hash/common/hash_api.h"

const bool IGNORE_LOAD_FACTOR = false;

class PiBenchDashHashAPI : public hash_api {
public:
  PiBenchDashHashAPI(uint64_t capacity, const std::string &shm_dir) {
    {
      base::file_util::CreateDirectory("/media/aep0/pibenchdash");
      dict_pool_name_ = "/media/aep0/pibenchdash/dict";
      dict_pool_size_ = 32 * 1024 * 1024 * 1024LL;

      LOG(ERROR) << "WARNING, we set 2x more memory for Dash";

      // 1.1: create (if not exist) and open the pool
      bool file_exist = false;
      if (FileExists(dict_pool_name_.c_str()))
        file_exist = true;
      Allocator::Initialize(dict_pool_name_.c_str(), dict_pool_size_);
      hash_table_ = reinterpret_cast<Hash<uint64_t> *>(
          Allocator::GetRoot(sizeof(extendible::Finger_EH<uint64_t>)));
      // 1.2: Initialize the hash table
      if (!file_exist) {
        // During initialization phase, allocate 64 segments for Dash-EH
        size_t segment_number = 64;
        new (hash_table_) extendible::Finger_EH<uint64_t>(
            segment_number, Allocator::Get()->pm_pool_);
      } else {
        new (hash_table_) extendible::Finger_EH<uint64_t>();
      }
    }
  }

  void xmhprintdebug() {}

  void thread_ini(int id) {}
  std::string hash_name() { return "PetHash"; };
  ~PiBenchDashHashAPI() {}
  hash_Utilization utilization() { return hash_Utilization(); }
  bool recovery() { return false; };
  /**
   * @brief Lookup record with given key.
   *
   * @param[in] key Pointer to beginning of key.
   * @param[in] sz Size of key in bytes.
   * @param[out] value_out Buffer to fill with value.
   * @return true if the key was found
   * @return false if the key was not found
   */
  virtual bool find(const char *key, size_t sz, char *value_out, unsigned tid) {

    auto epoch_guard = Allocator::AquireEpochGuard();
    Value_t read_value;
    if (hash_table_->Get(*(uint64_t *)key, &read_value, true) == false) {
      return false;
    } else {
      *(uint64_t *)value_out = (uint64_t)read_value;
      return true;
    }
  }

  /**
   * @brief Insert a record with given key and value.
   
   *
   * @param key Pointer to beginning of key.
   * @param key_sz Size of key in bytes.
   * @param value Pointer to beginning of value.
   * @param value_sz Size of value in bytes.
   * @return true if record was successfully inserted.
   * @return false if record was not inserted because it already exists.
   */
  virtual bool insert(const char *key, size_t key_sz, const char *value,
                      size_t value_sz, unsigned tid, unsigned t) {
    auto epoch_guard = Allocator::AquireEpochGuard();
    auto ret = hash_table_->Insert(*(uint64_t *)key, (char *)value, true);
    return true;
  }
  virtual bool insertResize(const char *key, size_t key_sz, const char *value,
                            size_t value_sz, unsigned tid, unsigned t) {
    auto ret = hash_table_->Insert(*(uint64_t *)key, (char *)value, true);
    return true;
  }
  /**
   * @brief Update the record with given key with the new given value.
   *
   * @param key Pointer to beginning of key.
   * @param key_sz Size of key in bytes.
   * @param value Pointer to beginning of new value.
   * @param value_sz Size of new value in bytes.
   * @return true if record was successfully updated.
   * @return false if record was not updated because it does not exist.
   */
  virtual bool update(const char *key, size_t key_sz, const char *value,
                      size_t value_sz) {
    auto ret = hash_table_->Insert(*(uint64_t *)key, (char *)value, true);
    return true;
  }

  /**
   * @brief Remove the record with the given key.
   *
   * @param key Pointer to the beginning of key.
   * @param key_sz Size of key in bytes.
   * @return true if key was successfully removed.
   * @return false if key did not exist.
   */
  virtual bool remove(const char *key, size_t key_sz, unsigned tid) {
    LOG(FATAL) << ";";
    //   dict_->Delete(*(uint64_t *)key);
    return true;
  }

  virtual int scan(const char *key, size_t key_sz, int scan_sz,
                   char *&values_out) {
    return true;
  }
  virtual int scan(const char *key, size_t key_sz, int scan_sz,
                   char *values_out) {
    return true;
  };
  Hash<uint64_t> *hash_table_;

  std::string dict_pool_name_;
  uint64_t dict_pool_size_;
};

extern "C" hash_api *create_hashtable(const hashtable_options_t &opt,
                                      unsigned sz, unsigned tnum) {
  ;

  return new PiBenchDashHashAPI(sz, opt.pool_path);
}