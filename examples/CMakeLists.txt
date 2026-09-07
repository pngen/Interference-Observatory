add_executable(io-examples example_scenarios.cpp)
target_link_libraries(io-examples PRIVATE InterferenceObservatory)
target_compile_options(io-examples PRIVATE ${IOBS_WARNING_FLAGS})

add_test(NAME examples COMMAND io-examples)
set_tests_properties(examples PROPERTIES TIMEOUT 60)
