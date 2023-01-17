#include "pet_hash.h"
// #include "third_party/HashEvaluation-for-petps/hash/common/hash_api.h"
#include "memory/shm_file.h"
#include "/home/xieminhui/HashEvaluation/hash/common/hash_api.h"

typedef base::PetHash<uint64, uint64> PiBenchPetHashDict;
const bool IGNORE_LOAD_FACTOR = false;

class PetHashAPI : public hash_api {
public:
  PetHashAPI(uint64_t capacity, const std::string &shm_dir) {
    auto dict_size = capacity;
    auto dict_memory_size =
        PiBenchPetHashDict::MemorySize(dict_size, IGNORE_LOAD_FACTOR);
    if (!dict_shm_file_.Initialize(shm_dir + "/dict", dict_memory_size)) {
      fs::remove(shm_dir + "/dict");
      CHECK(dict_shm_file_.Initialize(shm_dir + "/dict", dict_memory_size));
      LOG(INFO) << "Initialize shm dict size: " << dict_memory_size
                << ",  ShmKVDict Need: "
                << PiBenchPetHashDict::MemorySize(capacity * 2);
    }
    dict_ = reinterpret_cast<PiBenchPetHashDict *>(dict_shm_file_.Data());
    if (!dict_->Valid(dict_shm_file_.Size())) {
      dict_->Initialize(dict_size, IGNORE_LOAD_FACTOR);
    }
  }

  void xmhprintdebug() {
    LOG(INFO) << folly::sformat("LoadFactor : {}/{}={}", dict_->Size(),
                                dict_->Capacity(),
                                dict_->Size() / (float)dict_->Capacity());
    LOG(INFO) << "MemoryUtil: "
              << dict_->Size() * 16 /
                     (float)dict_shm_file_.Size();
  }

  void thread_ini(int id) {}
  std::string hash_name() { return "PetHash"; };
  ~PetHashAPI() {}
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
    auto [value, exists] = dict_->Get(*(uint64_t *)key);
    if (exists)
      *(uint64_t *)value_out = value;
    return exists;
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
    dict_->Set(*(uint64_t *)key, *(uint64_t *)value);
    return true;
  }
  virtual bool insertResize(const char *key, size_t key_sz, const char *value,
                            size_t value_sz, unsigned tid, unsigned t) {
    dict_->Set(*(uint64_t *)key, *(uint64_t *)value);
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
    dict_->Set(*(uint64_t *)key, *(uint64_t *)value);
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
    dict_->Delete(*(uint64_t *)key);
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
  base::ShmFile dict_shm_file_;
  PiBenchPetHashDict *dict_;
};

extern "C" hash_api *create_hashtable(const hashtable_options_t &opt,
                                      unsigned sz, unsigned tnum) {
  ;

  return new PetHashAPI(sz, opt.pool_path);
}