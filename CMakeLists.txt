cmake_minimum_required(VERSION 3.20)
project(leetcode_compare CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to an optimized build — these are benchmarks; a debug build would
# measure the wrong thing.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.7.1
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

include(CTest)
include(Catch)

# Header-only engine. Include paths are rooted at the project so headers read as
# "engine/..." and "problems/print-in-order/...".
add_library(engine INTERFACE)
target_include_directories(engine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine INTERFACE Threads::Threads)
target_compile_features(engine INTERFACE cxx_std_20)

add_subdirectory(problems/print-in-order)
