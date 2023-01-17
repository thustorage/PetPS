#include "library_loader.hpp"

#include <dlfcn.h>

#include <iostream>

namespace PiBench
{

  library_loader_t::library_loader_t(const std::string &path)
  {
    // Dynamically loads the library indicated by 'path'
    handle_ = dlopen(path.c_str(), RTLD_NOW);
    if (handle_ == nullptr)
    {
      std::cout << "Error in dlopen(): " << dlerror() << std::endl;
      exit(1);
    }

    // Search function 'create_hashtable'
    dlerror();
    create_fn_ = (hash_api * (*)(const hashtable_options_t &, unsigned, unsigned))
        dlsym(handle_, "create_hashtable");
    auto err = dlerror();
    if (err != nullptr)
    {
      std::cout << "Error in dlsym(): " << err << std::endl;
      exit(1);
    }
    else if (create_fn_ == nullptr)
    {
      std::cout << "Could not find 'create()'" << std::endl;
      exit(1);
    }
  }

  library_loader_t::~library_loader_t()
  {
    if (dlclose(handle_) != 0)
    {
      std::cout << "Error in dlclose()" << std::endl;
      return;
    }
  }

  hash_api *library_loader_t::create_hashtable(const hashtable_options_t &opt,
                                          unsigned sz = 0, unsigned tnum = 1)
  {
    return create_fn_(opt, sz, tnum);
  }
} // namespace PiBench