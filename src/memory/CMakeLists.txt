# TODO add_library(base_memory persist_malloc.cc loop_shm_malloc.cc shm_file.cc)
add_library(base_memory persist_malloc.cc shm_file.cc)
target_link_libraries(base_memory PUBLIC base)

add_executable(pm_command pm_command.cc)
target_link_libraries(pm_command PUBLIC base_memory)
