project(petps)
cmake_minimum_required(VERSION 3.8)

set(CMAKE_C_COMPILER "/usr/bin/gcc-9")
set(CMAKE_CXX_COMPILER "/usr/bin/g++-9")
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)


# Please modify the following path to your installed path.
set(folly_DIR "/home/xieminhui/folly-install/usr/local/lib/cmake/folly")
set(FOLLY_ROOT_DIR "/home/xieminhui/folly-install")
set(TBB_DIR "/home/xieminhui/intel/oneapi/tbb/latest/lib/cmake/tbb")

find_package(folly REQUIRED)
find_package(glog CONFIG REQUIRED)
find_package(OpenMP)
find_package(TBB REQUIRED)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/third_party/cmake)
find_package(Cityhash)




set(WARNING_FLAGS
    "-Wall -Werror \
 -Wno-long-long -Wno-unused-label \
-Wno-parentheses -Wno-unused-but-set-variable -Wno-unused-function \
-Wno-strict-aliasing -Wno-unknown-pragmas -Wno-sign-compare \
-Wno-write-strings -Wno-unused-result -Wno-unused-value \
-Wno-unused-parameter -Wno-unused-variable")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -ggdb3 -Wall -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -ggdb3 ${WARNING_FLAGS}")
set(CMAKE_CUDA_FLAGS
    "${CMAKE_CUDA_FLAGS} -g -ggdb3 -rdc=true -Xcompiler -Wall,-Werror,-Wno-error=cpp"
)

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -ggdb3 -O0 -Wall -Werror")
set(CMAKE_CXX_FLAGS_DEBUG
    "${CMAKE_CXX_FLAGS_DEBUG} -g -ggdb3 -O0 ${WARNING_FLAGS}")
set(CMAKE_CUDA_FLAGS_DEBUG
    "${CMAKE_CUDA_FLAGS_DEBUG} -O0 -G -Xcompiler -Wall,-Werror,-Wno-error=cpp,-g,-ggdb3"
)

set(CMAKE_CUDA_FLAGS
    "${CMAKE_CUDA_FLAGS} --expt-extended-lambda --expt-relaxed-constexpr")

# setting output folder
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_compile_definitions(SHM_PATH="/dev/shm")
# add_compile_definitions(SHM_PATH="/media/aep0/")
# add_compile_definitions(RPC_DEBUG)

enable_testing()

include_directories(${PROJECT_SOURCE_DIR}/)
add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/Mayfly-main)
add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/dash)
add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/HashEvaluation-for-petps)


add_subdirectory(src)
add_subdirectory(test)
