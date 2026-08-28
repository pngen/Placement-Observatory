cmake_minimum_required(VERSION 3.24)

project(PlacementObservatory VERSION 1.0.0 LANGUAGES CXX)

option(PO_BUILD_TESTS "Build the test suite" ON)
option(PO_BUILD_BENCHMARKS "Build the benchmark suite" ON)
option(PO_BUILD_EXAMPLES "Build the runnable examples" ON)
option(PO_BUILD_CLI "Build the po CLI" ON)
option(PO_ENABLE_CUDA "Enable the real CUDA/NVML provider (auto-detected)" ON)
option(PO_ENABLE_PROTOCOL "Enable the framed TCP multiprocess protocol" ON)
option(PO_WARNINGS_AS_ERRORS "Treat warnings as errors (/WX)" ON)

if(NOT MSVC)
  message(FATAL_ERROR "Placement Observatory is validated on MSVC (Visual Studio 2022).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC strictness is applied to C++ only so that nvcc's host compilation of
# CUDA code is not forced through MSVC-only flags that nvcc does not forward.
add_compile_options(
  $<$<COMPILE_LANGUAGE:CXX>:/W4>
  $<$<COMPILE_LANGUAGE:CXX>:/permissive->
  $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
  $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
  $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
  $<$<COMPILE_LANGUAGE:CXX>:/std:c++20>
)
if(PO_WARNINGS_AS_ERRORS)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/WX>)
endif()

if(MSVC_VERSION LESS 1930)
  message(FATAL_ERROR "MSVC 19.30+ required; found ${MSVC_VERSION}")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# CUDA detection (optional)
set(PO_CUDA_FOUND FALSE)
if(PO_ENABLE_CUDA)
  find_package(CUDAToolkit QUIET)
  if(CUDAToolkit_FOUND)
    set(PO_CUDA_FOUND TRUE)
  else()
    message(STATUS "CUDA toolkit not found - building CPU-only.")
  endif()
endif()

# Library target
file(GLOB_RECURSE PO_LIB_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(PlacementObservatory ${PO_LIB_SOURCES})
add_library(PlacementObservatory::PlacementObservatory ALIAS PlacementObservatory)

target_include_directories(PlacementObservatory PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_definitions(PlacementObservatory PRIVATE
  _WIN32_WINNT=0x0A00
  WIN32_LEAN_AND_MEAN
)
target_link_libraries(PlacementObservatory PUBLIC
  ws2_32
  $<$<PLATFORM_ID:Windows>:bcrypt>
)

if(PO_ENABLE_PROTOCOL)
  target_compile_definitions(PlacementObservatory PUBLIC PO_ENABLE_PROTOCOL=1)
endif()

set_target_properties(PlacementObservatory PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME PlacementObservatory
)

if(PO_CUDA_FOUND)
  set(CMAKE_CUDA_STANDARD 20)
  set(CMAKE_CUDA_STANDARD_REQUIRED ON)
  enable_language(CUDA)
  set(CMAKE_CUDA_ARCHITECTURES 120)
endif()

# --- CUDA provider library (optional; real RTX 5090 device evidence) ---
if(PO_CUDA_FOUND)
  add_library(PlacementObservatoryCUDA STATIC src/providers/cuda/cuda_provider.cu)
  add_library(PlacementObservatory::PlacementObservatoryCUDA ALIAS PlacementObservatoryCUDA)
  target_include_directories(PlacementObservatoryCUDA PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_link_libraries(PlacementObservatoryCUDA PUBLIC PlacementObservatory CUDA::cudart)
  set_target_properties(PlacementObservatoryCUDA PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CUDA_STANDARD 20
    CUDA_ARCHITECTURES 120
    EXPORT_NAME PlacementObservatoryCUDA)
  target_compile_options(PlacementObservatoryCUDA PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/W4>
    $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/permissive->
    $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/Zc:__cplusplus>
    # C4211 is emitted only by nvcc's own generated device stub, never our code.
    $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/wd4211>)
  install(TARGETS PlacementObservatoryCUDA EXPORT PlacementObservatoryTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

# ============================================================================
# Subsystem directories (tests / benchmarks / examples / CLI)
# ============================================================================
if(PO_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(PO_BUILD_BENCHMARKS)
  add_subdirectory(bench)
endif()
if(PO_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(PO_BUILD_CLI)
  add_subdirectory(cli)
endif()

install(TARGETS PlacementObservatory
  EXPORT PlacementObservatoryTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/placement_observatory"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PlacementObservatoryConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/PlacementObservatoryConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/PlacementObservatory"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/PlacementObservatoryConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(EXPORT PlacementObservatoryTargets
  FILE PlacementObservatoryTargets.cmake
  NAMESPACE PlacementObservatory::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/PlacementObservatory"
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/PlacementObservatoryConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/PlacementObservatoryConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/PlacementObservatory"
)