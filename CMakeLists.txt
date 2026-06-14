cmake_minimum_required(VERSION 3.28)

project(leetcode_compare
  VERSION 0.2.0
  DESCRIPTION "Benchmark harness comparing LeetCode solutions on time, memory & object size"
  LANGUAGES CXX)

# Modern C++ across the whole tree.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# These are benchmarks; a debug build would measure the wrong thing.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo)
endif()

find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.7.1
  GIT_SHALLOW    TRUE
  SYSTEM                       # silence warnings from Catch2's own headers
  EXCLUDE_FROM_ALL             # don't install/build Catch2 unless something uses it
  FIND_PACKAGE_ARGS NAMES Catch2 # prefer a system Catch2 if one is installed
)
FetchContent_MakeAvailable(Catch2)
if(catch2_SOURCE_DIR)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

include(CTest)
include(Catch)

# Header-only engine. Include paths are rooted at the project so headers read as
# "engine/..." and "problems/print-in-order/...".
add_library(engine INTERFACE)
target_include_directories(engine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine INTERFACE Threads::Threads)
target_compile_features(engine INTERFACE cxx_std_23)

add_subdirectory(problems/print-in-order)
