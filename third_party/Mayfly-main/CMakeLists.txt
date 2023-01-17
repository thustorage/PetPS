cmake_minimum_required(VERSION 2.8.5 FATAL_ERROR)
project(Mayfly)

# set (CMAKE_C_COMPILER "g++")
# set (CMAKE_CXX_COMPILER ${CMAKE_C_COMPILER})

#Compiler options
# set(CMAKE_C_FLAGS "-Wall -Wno-deprecated-declarations -Wsign-compare -DNDEBUG -g")
# set(CMAKE_C_FLAGS "-Wall -Wno-deprecated-declarations -Wsign-compare  -DNDEBUG -march=native -O3 -g")
# set(CMAKE_C_FLAGS "-Wall -Wno-deprecated-declarations -Wsign-compare  -march=native -O3 -g")
# set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++11 ")


option (SANITIZE "Turn on sanitization" OFF)
if (SANITIZE)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
endif()

set(INCLUDE_BASE ${PROJECT_SOURCE_DIR}/include)
include_directories(${INCLUDE_BASE})


set(MAYFLY_PATH ${PROJECT_SOURCE_DIR})
configure_file(${PROJECT_SOURCE_DIR}/mayfly_config.h.in
${PROJECT_BINARY_DIR}/mayfly_config.h
)



#Source file define
set(COMMON_SRC ${PROJECT_SOURCE_DIR}/src)

#Used by both server and clients
file(GLOB_RECURSE COMMON_FILE ${COMMON_SRC}/*.cpp)
add_library(mayfly STATIC ${COMMON_FILE})
target_link_libraries(mayfly PUBLIC pthread ibverbs memcached profiler)
link_libraries(mayfly)
target_include_directories(mayfly PUBLIC ${PROJECT_BINARY_DIR})

file(GLOB TEST_SRC ${PROJECT_SOURCE_DIR}/test/*.cpp)
foreach (TEST ${TEST_SRC})
    get_filename_component(TEST_NAME ${TEST} NAME_WE)
    add_executable(${TEST_NAME} ${TEST})
    target_link_libraries(${TEST_NAME} ${LINKS_FLAGS})
endforeach()

