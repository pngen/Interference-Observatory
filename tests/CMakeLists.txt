# Core test suite. No test uses a timeout; a hanging test is a defect.

function(iobs_add_test name)
  add_executable(${name} test_frame_main.cpp ${name}.cpp)
  target_link_libraries(${name} PRIVATE InterferenceObservatory)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(${name} PRIVATE ${IOBS_WARNING_FLAGS})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

iobs_add_test(test_core)
iobs_add_test(test_persistence)
iobs_add_test(test_adversarial)
iobs_add_test(test_concurrency)
iobs_add_test(test_property)
