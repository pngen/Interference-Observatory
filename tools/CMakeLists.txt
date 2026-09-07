# CLI and distributed proof tools.

add_executable(io-observatory io-observatory.cpp)
target_link_libraries(io-observatory PRIVATE InterferenceObservatory)
target_compile_options(io-observatory PRIVATE ${IOBS_WARNING_FLAGS})

# Distributed control-plane proof: coordinator and workers over real framed TCP.
add_executable(io-coordinator io-coordinator.cpp)
target_link_libraries(io-coordinator PRIVATE InterferenceObservatory)
target_compile_options(io-coordinator PRIVATE ${IOBS_WARNING_FLAGS})

add_executable(io-worker io-worker.cpp)
target_link_libraries(io-worker PRIVATE InterferenceObservatory)
target_compile_options(io-worker PRIVATE ${IOBS_WARNING_FLAGS})

# Real NVIDIA/CUDA controlled-interference experiments (built only when CUDA is available).
if(TARGET InterferenceObservatoryCUDA)
  add_executable(io-cuda-experiment io-cuda-experiment.cpp)
  target_link_libraries(io-cuda-experiment PRIVATE InterferenceObservatory InterferenceObservatoryCUDA)
  target_compile_options(io-cuda-experiment PRIVATE ${IOBS_WARNING_FLAGS})
  target_compile_definitions(io-cuda-experiment PRIVATE IOBS_HAS_CUDA=1)
endif()

add_test(NAME distributed_proof
  COMMAND io-coordinator --port 39116 --worker-exe $<TARGET_FILE:io-worker>
          --state ${CMAKE_CURRENT_BINARY_DIR}/dist.bin --epoch 1)
set_tests_properties(distributed_proof PROPERTIES TIMEOUT 120)

if(TARGET io-cuda-experiment)
  add_test(NAME cuda_proof COMMAND io-cuda-experiment)
  set_tests_properties(cuda_proof PROPERTIES TIMEOUT 120)
endif()
