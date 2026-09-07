add_executable(io-bench bench.cpp)
target_link_libraries(io-bench PRIVATE InterferenceObservatory)
target_compile_options(io-bench PRIVATE ${IOBS_WARNING_FLAGS})
