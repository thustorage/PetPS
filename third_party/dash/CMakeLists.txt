# cd ~/HugeCTR/third_party/nvmps/third_party/dash/third_party/pmdk/src/PMDK
# export
# PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/home/linuxbrew/.linuxbrew/lib/pkgconfig:/usr/share/pkgconfig
# make EXTRA_CFLAGS=-Wno-error -j

# set(CMAKE_C_COMPILER "/usr/bin/gcc") set(CMAKE_CXX_COMPILER "/usr/bin/g++")

if(POLICY CMP0048)
  cmake_policy(SET CMP0048 NEW)
endif()

project(dash)
cmake_minimum_required(VERSION 3.11)
include(FetchContent)
include(ExternalProject)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -Wno-long-long -Wno-unused-label -Wno-parentheses -Wno-unused-but-set-variable -fPIC -march=native"
)
# set(CMAKE_ENABLE_COMPILE_COMMANDS "ON")

# if(${CMAKE_BUILD_TYPE} STREQUAL "Release") set(CMAKE_CXX_FLAGS
# "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG -Ofast") endif()

find_package(folly REQUIRED)

add_library(dash_hash INTERFACE)

# #################### PMDK ####################
set(PMDK_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pmdk")
ExternalProject_Add(
  PMDK
  GIT_REPOSITORY https://github.com/HaoPatrick/pmdk.git
  GIT_TAG addr-patch
  BUILD_IN_SOURCE 1
  # BUILD_COMMAND
  # PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/home/linuxbrew/.linuxbrew/lib/pkgconfig:/usr/share/pkgconfig
  # $(MAKE) EXTRA_CFLAGS=-Wno-error -j BUILD_COMMAND "$(MAKE)
  # EXTRA_CFLAGS=-Wno-error -j"
  BUILD_COMMAND ""
  PREFIX ${PMDK_PREFIX}
  CONFIGURE_COMMAND ""
  INSTALL_COMMAND ""
  # PATCH_COMMAND "sed ${PMDK_PREFIX}/src/ "
  LOG_DOWNLOAD ON
  LOG_CONFIGURE ON
  LOG_BUILD ON)

set(PMDK_LIB_DIR ${PMDK_PREFIX}/src/PMDK/src/nondebug)
# set(PMDK_LIB_DIR ${PMDK_PREFIX}/src/PMDK/src/debug)

add_custom_target(
  compile_pmdk
  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/gen_dash_pmdk.sh")

target_include_directories(dash_hash
                           INTERFACE ${PMDK_PREFIX}/src/PMDK/src/include)

# #################### Epoch Reclaim ####################
FetchContent_Declare(
  epoch_reclaimer
  GIT_REPOSITORY https://github.com/XiangpengHao/epoch-reclaimer.git
  GIT_TAG master)
if(NOT epoch_reclaimer_POPULATED)
  FetchContent_Populate(epoch_reclaimer)
  target_include_directories(dash_hash INTERFACE ${epoch_reclaimer_SOURCE_DIR})
endif()

option(USE_PMEM "enable persistent memory support" ON)

set(libs_to_link pthread gflags Folly::folly -pthread)

if(USE_PMEM MATCHES "ON")
  message(STATUS "persistent memory support enabled, going to build with PMDK")
  add_definitions(-DPMEM)
  list(APPEND libs_to_link ${PMDK_LIB_DIR}/libpmemobj.a
       ${PMDK_LIB_DIR}/libpmem.a ndctl daxctl)
endif()

target_link_libraries(dash_hash INTERFACE ${libs_to_link})
if(USE_PMEM MATCHES "ON")
  target_compile_definitions(dash_hash INTERFACE -DPMEM)
endif()
target_compile_options(dash_hash INTERFACE -msse -msse2 -msse3 -msse4 -mavx)

add_dependencies(compile_pmdk PMDK)
add_dependencies(dash_hash compile_pmdk)

if(USE_PMEM MATCHES "ON")
  add_executable(test_pmem src/test_pmem.cpp)
  add_executable(example src/example.cpp)
  target_link_libraries(test_pmem PRIVATE dash_hash)
  target_link_libraries(example PRIVATE dash_hash)
else()

endif()
