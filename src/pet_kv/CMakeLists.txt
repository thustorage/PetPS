add_library(pet_kv pet_kv.cc)
target_link_libraries(pet_kv PUBLIC base base_memory)
target_compile_options(pet_kv PUBLIC -msse -msse2 -msse3 -msse4)
target_compile_options(pet_kv PUBLIC -Wno-error=class-memaccess)


add_library(
  PiBenchPetHash SHARED
  PiBenchWrapper.cc
  # pet_kv.cc
  # old_shm_kv.cc
)
target_compile_definitions(PiBenchPetHash PRIVATE USED_IN_PI_BENCH)
target_link_libraries(PiBenchPetHash PRIVATE base base_memory)
target_compile_options(PiBenchPetHash PRIVATE -msse -msse2 -msse3 -msse4)
target_compile_options(PiBenchPetHash PRIVATE -Wno-error=class-memaccess)

add_library(PiBenchDash SHARED ${PROJECT_SOURCE_DIR}/src/kv_engine/PiBenchWrapperDash.cc)
target_link_libraries(PiBenchDash PRIVATE dash_hash base)

add_custom_command(
  TARGET PiBenchPetHash
  POST_BUILD
  COMMAND cp "$<TARGET_FILE:PiBenchPetHash>" "/home/xieminhui/HashEvaluation/bin/")

add_custom_command(
  TARGET PiBenchDash
  POST_BUILD
  COMMAND cp "$<TARGET_FILE:PiBenchDash>" "/home/xieminhui/HashEvaluation/bin/")