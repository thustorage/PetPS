function(xmh_gtest)
  foreach(testcase ${ARGV})
    add_executable(${testcase} ${testcase}.cc)
    target_link_libraries(${testcase} PUBLIC pet_kv gtest_main)
    gtest_discover_tests(${testcase})
  endforeach()
endfunction()

include_directories(${PROJECT_SOURCE_DIR}/src)

xmh_gtest(persist_memorypool_test persist_pet_hash_test
          petkv_multithread_test)

add_executable(kv_engine_test kv_engine_test.cc)
target_link_libraries(kv_engine_test PUBLIC kv_engine base gtest_main)
gtest_discover_tests(kv_engine_test)
