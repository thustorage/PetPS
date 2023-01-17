project(PMHashEvaluation)

FILE(GLOB ALL_SOURCES "${PROJECT_SOURCE_DIR}/hash/common/*.cpp")
FILE(GLOB ALL_INCLUDES "${PROJECT_SOURCE_DIR}/hash/common/*.h")

SET(DIR_SRCS
        ${ALL_SOURCES}
        ${ALL_INCLUDES}
        )

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++17 -Wno-sign-compare  -Wno-unused-variable -Wno-unused-but-set-variable \
-Wno-uninitialized -Wno-mismatched-new-delete -Werror=maybe-uninitialized -Wno-maybe-uninitialized -fPIC -Wno-long-long  -march=native")
add_library(
        PMHashPCLHT STATIC
        ${DIR_SRCS}
        ${PROJECT_SOURCE_DIR}/hash/PCLHT/clht_lb_res.cpp
)
target_compile_definitions(PMHashPCLHT PRIVATE -DCLWB -DPMEM=1)
target_include_directories(PMHashPCLHT PRIVATE
        ${PROJECT_SOURCE_DIR}/hash/common/
        ${PROJECT_SOURCE_DIR}/dash/src/
        ${PROJECT_SOURCE_DIR}/common/
        ${PROJECT_SOURCE_DIR}/hash/PCLHT)
target_link_libraries(PMHashPCLHT PUBLIC vmem pmemobj pthread dash_hash)
# target_link_libraries(PMHashPCLHT PUBLIC vmem pmemobj pthread)


# set(xmh_maybe_uninitialized "-Wno-maybe-uninitialized")
set(xmh_maybe_uninitialized "-Wno-uninitialized")


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++17 -Wno-sign-compare \
-Wno-unused-variable -Wno-unused-but-set-variable \
${xmh_maybe_uninitialized} -Wno-mismatched-new-delete -fPIC "
        )

add_library(
        PMHashLevel STATIC
        ${DIR_SRCS}
        ${PROJECT_SOURCE_DIR}/hash/Level/Level_hashing.cpp
)
target_compile_definitions(PMHashLevel PRIVATE -DCLWB)
target_include_directories(PMHashLevel PRIVATE
        ${PROJECT_SOURCE_DIR}/hash/common
        ${PROJECT_SOURCE_DIR}/dash/src/
        )
target_link_libraries(PMHashLevel PUBLIC vmem pmemobj pthread dash_hash)


add_library(
        PMHashClevel STATIC
        ${DIR_SRCS}
        ${PROJECT_SOURCE_DIR}/hash/Clevel/tests/clevel_hash/clevel_hash_cli.cpp
)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++17 -Wno-unused-but-set-variable  ${xmh_maybe_uninitialized} -fPIC ")
target_compile_definitions(PMHashClevel PRIVATE -DCLWB)
target_include_directories(PMHashClevel PRIVATE
        ${PROJECT_SOURCE_DIR}/hash/common
        ${PROJECT_SOURCE_DIR}/hash/Clevel/
        ${PROJECT_SOURCE_DIR}/hash/Clevel/test/
        /usr/local/include/valgrind)
target_link_libraries(PMHashClevel PUBLIC vmem pmemobj pthread)


# set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=gnu++17 -Wno-sign-compare  -Wno-unused-variable -Wno-unused-but-set-variable -Wno-maybe-uninitialized -Wno-mismatched-new-delete -fPIC ")
# add_library(
# 	PMHashCCEHVM STATIC 
# ${DIR_SRCS}
# ${PROJECT_SOURCE_DIR}/hash/CCEH/CCEH_MSB.cpp
# )
# target_compile_definitions(PMHashCCEHVM PRIVATE -DCLWB -DPMEM=1)
# target_include_directories(PMHashCCEHVM PRIVATE
# 	${PROJECT_SOURCE_DIR}/hash/common
# 	${PROJECT_SOURCE_DIR}/hash/CCEH/
# 	${PROJECT_SOURCE_DIR}/dash/src/
# 	${PROJECT_SOURCE_DIR}/common/
# )
# target_link_libraries(PMHashCCEHVM PUBLIC vmem pmemobj pthread dash_hash)


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++17 -Wno-sign-compare  -Wno-unused-variable -Wno-unused-but-set-variable ${xmh_maybe_uninitialized} -Wno-mismatched-new-delete -fPIC ")
add_library(
        PMHashCCEHVM STATIC
        ${DIR_SRCS}
        ${PROJECT_SOURCE_DIR}/hash/CCEH-vm/CCEH_MSB.cpp
)
target_compile_definitions(PMHashCCEHVM PRIVATE -DCLWB -DPMEM=1)
target_include_directories(PMHashCCEHVM PRIVATE
        ${PROJECT_SOURCE_DIR}/hash/common
        ${PROJECT_SOURCE_DIR}/hash/CCEH-vm/
        ${PROJECT_SOURCE_DIR}/dash/src/
        ${PROJECT_SOURCE_DIR}/common/
        )
target_link_libraries(PMHashCCEHVM PUBLIC vmem pmemobj pthread dash_hash)