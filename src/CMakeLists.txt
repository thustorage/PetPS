include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(base)
add_subdirectory(memory)
add_subdirectory(pet_kv)
# add_subdirectory(ks_shm_kv)
add_subdirectory(kv_engine)
add_subdirectory(ps)
add_subdirectory(benchmark)