
add_executable(perf_sgl perf_sgl.cc)
target_link_libraries(perf_sgl PUBLIC base ${warp_wq_ps_client} mayfly)
target_compile_options(perf_sgl PUBLIC -fopenmp)
target_link_options(perf_sgl PUBLIC -fopenmp)

add_executable(dataset_wordcount_helper dataset_wordcount.cc)
target_link_libraries(dataset_wordcount_helper PUBLIC pthread base)

add_executable(dataset_renumber_id_helper dataset_renumbering.cc)
target_link_libraries(dataset_renumber_id_helper PUBLIC pthread base)
target_link_libraries(dataset_renumber_id_helper PUBLIC OpenMP::OpenMP_CXX)
target_link_libraries(dataset_renumber_id_helper PUBLIC TBB::tbb)

add_executable(benchmark_client benchmark_client.cc)
target_link_libraries(benchmark_client PUBLIC ${warp_wq_ps_client} base)
target_link_libraries(benchmark_client PUBLIC OpenMP::OpenMP_CXX)