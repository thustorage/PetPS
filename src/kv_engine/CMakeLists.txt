find_package(PkgConfig QUIET)

option(USE_PMKV "enable PMKV" OFF)
if(USE_PMKV MATCHES "ON")
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBPMEMKV REQUIRED libpmemkv)
  else()
    message(FATAL_ERROR "pkg-config not found")
  endif()

  include_directories(${LIBPMEMKV_INCLUDE_DIRS})
  link_directories(${LIBPMEMKV_LIBRARY_DIRS})
endif()

# KV engine factory
add_library(kv_engine INTERFACE)
target_link_libraries(kv_engine INTERFACE base)
if(USE_PMKV MATCHES "ON")
  target_link_libraries(kv_engine INTERFACE ${LIBPMEMKV_LIBRARIES})
endif()

target_link_libraries(
  kv_engine
  INTERFACE dash_hash pet_kv PMHashPCLHT PMHashLevel PMHashClevel
            PMHashCCEHVM)

target_sources(
  kv_engine INTERFACE ${PROJECT_SOURCE_DIR}/src/kv_engine/kv_engine_register.cc)
# KV engine factory done

add_executable(perf_kv_engine benchmark_kv_engine.cc ../ps/Postoffice.cc)
target_link_libraries(perf_kv_engine PUBLIC pthread base kv_engine mayfly)
target_link_libraries(perf_kv_engine PUBLIC OpenMP::OpenMP_CXX)

add_executable(bench benchmark_zipf.cc)
target_link_libraries(bench PUBLIC pthread base kv_engine)
