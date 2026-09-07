cmake_minimum_required(VERSION 3.25)
project(InterferenceObservatory VERSION 1.0.0 LANGUAGES CXX)

option(IOBS_BUILD_TESTS "Build tests" ON)
option(IOBS_BUILD_TOOLS "Build CLI / distributed workers and coordinator" ON)
option(IOBS_BUILD_EXAMPLES "Build examples" ON)
option(IOBS_BUILD_BENCHMARKS "Build benchmarks" ON)
option(IOBS_BUILD_CUDA "Build the real NVIDIA/CUDA backend" ON)
option(IOBS_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(IOBS_WARNING_FLAGS "")
if(MSVC)
  set(IOBS_WARNING_FLAGS /W4)
  if(IOBS_WARNINGS_AS_ERRORS)
    list(APPEND IOBS_WARNING_FLAGS /WX)
  endif()
else()
  set(IOBS_WARNING_FLAGS -Wall -Wextra)
  if(IOBS_WARNINGS_AS_ERRORS)
    list(APPEND IOBS_WARNING_FLAGS -Werror)
  endif()
endif()

add_library(InterferenceObservatory STATIC
  src/capability.cpp
  src/persist.cpp
  src/protocol.cpp
  src/observatory.cpp
)
add_library(InterferenceObservatory::InterferenceObservatory ALIAS InterferenceObservatory)

target_include_directories(InterferenceObservatory PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
if(WIN32)
  target_link_libraries(InterferenceObservatory PUBLIC ws2_32)
endif()
target_compile_options(InterferenceObservatory PRIVATE ${IOBS_WARNING_FLAGS})
set_target_properties(InterferenceObservatory PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(IOBS_BUILD_CUDA)
  set(IOBS_CUDA_FOUND FALSE)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND AND CUDAToolkit_NVCC_EXECUTABLE)
      set(IOBS_CUDA_FOUND TRUE)
    endif()
  endif()
  if(IOBS_CUDA_FOUND)
    # Compile the isolated CUDA backend with nvcc directly (Ninja/MSVC CMake CUDA integration
    # mis-generates MSVC PDB flags on this toolchain). CUDA stays a separate, optional library.
    set(IOBS_CUDA_OBJ ${CMAKE_CURRENT_BINARY_DIR}/cuda_backend.obj)
    # Match the MSVC runtime library used by the host build (default is dynamic CRT).
    if(MSVC)
      if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(IOBS_CUDA_RT "/MDd")
      else()
        set(IOBS_CUDA_RT "/MD")
      endif()
    else()
      set(IOBS_CUDA_RT "")
    endif()
    add_custom_command(
      OUTPUT ${IOBS_CUDA_OBJ}
      COMMAND ${CUDAToolkit_NVCC_EXECUTABLE} -c -x cu -std=c++20
              --generate-code=arch=compute_120,code=sm_120
              -Xcompiler=${IOBS_CUDA_RT}
              -I${CMAKE_CURRENT_SOURCE_DIR}/include -I${CUDAToolkit_INCLUDE_DIRS}
              ${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/cuda_backend.cu -o ${IOBS_CUDA_OBJ}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/cuda_backend.cu
      COMMENT "CUDA compile cuda_backend.cu (sm_120)"
    )
    add_library(InterferenceObservatoryCUDA STATIC ${IOBS_CUDA_OBJ})
    add_library(InterferenceObservatory::CUDA ALIAS InterferenceObservatoryCUDA)
    target_include_directories(InterferenceObservatoryCUDA PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
    target_link_libraries(InterferenceObservatoryCUDA PUBLIC CUDA::cudart InterferenceObservatory)
    set_target_properties(InterferenceObservatoryCUDA PROPERTIES
      POSITION_INDEPENDENT_CODE ON CUDA_ARCHITECTURES 120)
    message(STATUS "Interference Observatory: CUDA backend enabled (nvcc ${CUDAToolkit_VERSION})")
  else()
    message(STATUS "Interference Observatory: CUDA backend DISABLED (CUDAToolkit not found)")
  endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS InterferenceObservatory
  EXPORT InterferenceObservatoryTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(TARGET InterferenceObservatoryCUDA)
  install(TARGETS InterferenceObservatoryCUDA
    EXPORT InterferenceObservatoryTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT InterferenceObservatoryTargets
  FILE InterferenceObservatoryTargets.cmake
  NAMESPACE InterferenceObservatory::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/InterferenceObservatory
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/InterferenceObservatoryConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/InterferenceObservatoryConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/InterferenceObservatory
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/InterferenceObservatoryConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/InterferenceObservatoryConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/InterferenceObservatoryConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/InterferenceObservatory
)

if(IOBS_BUILD_TESTS)
  enable_testing()
endif()

if(IOBS_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
if(IOBS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(IOBS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(IOBS_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
