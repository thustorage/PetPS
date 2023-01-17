/**
 * Hashtable implementations must follow the API defined in this file, which consists
 * of two main parts:
 *     1. create_hashtable(...) function that is responsible for instantiating the
 *        hashtable. The function will be called with an instance of hashtable_options_t passed
 *        as parameter. This object constains information that can be used for
 *        instantiating specialized instances of the hashtable (e.g. inlinining small
 *        keys and values). This function must return a pointer to a class that
 *        inherits from hash_api (see below).
 *     2. hash_api class covers the main methods to be called on the hashtable by the
 *        benchmark framework. Keys and values are passed in a generalized way
 *        using a C-like syntax to enable an easier integration of a wider
 *        variety of hashtable implementations.
 */
#ifndef __hash_api_HPP__
#define __hash_api_HPP__

#include <cstddef>
#include <iostream>
#include <string>

struct hashtable_options_t {
  size_t key_size = 8;
  size_t value_size = 8;
  std::string pool_path = "";
  size_t pool_size = 0;
  size_t num_threads = 1;
};
struct hash_Utilization {
  float load_factor;
  float utilization;
};
class hash_api;
extern "C" hash_api *create_hashtable(const hashtable_options_t &opt, unsigned sz,
                                 unsigned tnum);

class hash_api {
 public:
  virtual void thread_ini(int id) {}
  virtual void forsoft() {}
  virtual std::string hash_name() { return ""; };
  virtual ~hash_api() {}
  virtual hash_Utilization utilization() { return hash_Utilization(); }
  virtual void print_util() { std::cout << "load_factor " << utilization().load_factor << " utilization " << utilization().utilization << std::endl;}
  virtual bool recovery() { return false; };
  virtual void vmem_print_api(){};
  /**
   * @brief Lookup record with given key.
   *
   * @param[in] key Pointer to beginning of key.
   * @param[in] sz Size of key in bytes.
   * @param[out] value_out Buffer to fill with value.
   * @return true if the key was found
   * @return false if the key was not found
   */
  virtual bool find(const char *key, size_t sz, char *value_out,
                    unsigned tid) = 0;

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
                      size_t value_sz, unsigned tid, unsigned t) = 0;
  virtual bool insertResize(const char *key, size_t key_sz, const char *value,
                            size_t value_sz, unsigned tid, unsigned t) {
    return false;
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
                      size_t value_sz) = 0;

  /**
   * @brief Remove the record with the given key.
   *
   * @param key Pointer to the beginning of key.
   * @param key_sz Size of key in bytes.
   * @return true if key was successfully removed.
   * @return false if key did not exist.
   */
  virtual bool remove(const char *key, size_t key_sz, unsigned tid) = 0;

  /**
   * @brief Scan records starting from record with given key.
   *
   * @param[in] key Pointer to the beginning of key of first record.
   * @param[in] key_sz Size of key in bytes of first record.
   * @param[in] scan_sz Amount of following records to be scanned.
   * @param[out] values_out Pointer to location of scanned records.
   * @return int Amount of records scanned.
   *
   * The implementation of scan must set 'values_out' internally to point to
   * a memory region containing the resulting records. The wrapper must
   * guarantee that this memory region is not deallocated and that access to
   * it is protected (i.e., not modified by other threads). The expected
   * contents of the memory is a contiguous sequence of <key><value>
   * representing the scanned records in ascending key order.
   *
   * A simple implementation of the scan method could be something like:
   *
   * static thread_local std::vector<std::pair<K,V>> results;
   * results.clear();
   *
   * auto it = tree.lower_bound(key);
   *
   * int scanned;
   * for(scanned=0; (scanned < scan_sz) && (it != map_.end()); ++scanned,++it)
   *     results.push_back(std::make_pair(it->first, it->second));
   *
   * values_out = results.data();
   * return scanned;
   */
  virtual int scan(const char *key, size_t key_sz, int scan_sz,
                   char *&values_out) = 0;
  virtual int scan(const char *key, size_t key_sz, int scan_sz,
                   char *values_out) {
    return true;
  };
};

#endif