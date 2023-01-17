#ifndef __LIBRARY_LOADER_HPP__
#define __LIBRARY_LOADER_HPP__

#include <dlfcn.h>

#include <string>

#include "hash_api.h"

namespace PiBench
{

  class library_loader_t
  {
  public:
    /**
   * @brief Construct a new library loader object
   *
   * @param path Absolute path of library file to be loaded.
   */
    library_loader_t(const std::string &path);

    /**
   * @brief Destroy the library loader object
   *
   */
    ~library_loader_t();

    /**
   * @brief Create a tree object
   *
   * Call create_hashtable function implemented by the library.
   *
   * @param tree_opt workload options useful for optimizing tree layout.
   * @return hash_api*
   */
    hash_api *create_hashtable(const hashtable_options_t &tree_opt, unsigned sz, unsigned tnum);

  private:
    /// Handle for the dynamic library loaded.
    void *handle_;

    /// Pointer to factory function resposinble for instantiating a tree.
    hash_api *(*create_fn_)(const hashtable_options_t &, unsigned, unsigned);
  };
} // namespace PiBench
#endif