cmake_minimum_required(VERSION 3.28)

project(sync_shootout
  VERSION 0.1.0
  DESCRIPTION "Benchmark shootout of synchronization primitives across scenarios"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# These are benchmarks; a debug build would measure the wrong thing.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo)
endif()

find_package(Threads REQUIRED)

# Header-only library: the primitives, scenarios and metrics.
add_library(shootout_headers INTERFACE)
target_include_directories(shootout_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(shootout_headers INTERFACE Threads::Threads)
target_compile_features(shootout_headers INTERFACE cxx_std_23)

# The benchmark driver — built optimized; this is what the sweep script runs.
add_executable(shootout src/shootout.cpp)
target_link_libraries(shootout PRIVATE shootout_headers)
target_compile_options(shootout PRIVATE -O3 -DNDEBUG -march=native)

# Correctness tests (Catch2).
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.7.1
  GIT_SHALLOW    TRUE
  SYSTEM
  EXCLUDE_FROM_ALL
  FIND_PACKAGE_ARGS NAMES Catch2
)
FetchContent_MakeAvailable(Catch2)
if(catch2_SOURCE_DIR)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

include(CTest)
include(Catch)
add_executable(shootout_tests src/tests.cpp)
target_link_libraries(shootout_tests PRIVATE shootout_headers Catch2::Catch2WithMain)
catch_discover_tests(shootout_tests)
