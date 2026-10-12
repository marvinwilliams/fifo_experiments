cmake_minimum_required(VERSION 3.18)

project(
  fifo_experiments
  VERSION 0.1
  LANGUAGES CXX C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(utils)

disable_intree_builds()

# Read the system's L1 cache-line size and page size
execute_process(
  COMMAND getconf LEVEL1_DCACHE_LINESIZE
  OUTPUT_VARIABLE DEFAULT_L1_CACHE_LINE_SIZE
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
  COMMAND getconf PAGESIZE
  OUTPUT_VARIABLE DEFAULT_PAGE_SIZE
  OUTPUT_STRIP_TRAILING_WHITESPACE)

find_package(PAPI)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads)

if(NOT PAPI_FOUND)
  message(STATUS "PAPI library not found, performance counters not available")
endif()

option("EXP_BUILD_TESTS" "Build unit tests for experiment tools" OFF)
set(L1_CACHE_LINE_SIZE
    ${DEFAULT_L1_CACHE_LINE_SIZE}
    CACHE STRING "The assumed L1 cache line size (bytes)")
set(PAGE_SIZE
    ${DEFAULT_PAGE_SIZE}
    CACHE STRING "The assumed page size (bytes)")
option(EXP_JEMALLOC "Link the benchmarks against jemalloc" OFF)

if(NOT "${L1_CACHE_LINE_SIZE}" MATCHES "^[0-9]+")
  message(SEND_ERROR "Invalid L1 cache line size")
endif()
if(NOT "${PAGE_SIZE}" MATCHES "^[0-9]+")
  message(SEND_ERROR "Invalid page size")
endif()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

include(FetchContent)
# Use a local checkout with -DFETCHCONTENT_SOURCE_DIR_BLOCKFIFO=<path>
# TODO: Pin to a commit once the header-only layout of the BlockFIFO is pushed
FetchContent_Declare(
  blockfifo
  GIT_REPOSITORY git@github.com:marvinwilliams/blockfifo.git
  GIT_TAG main
  GIT_SHALLOW TRUE
  GIT_SUBMODULES "")
FetchContent_MakeAvailable(blockfifo)
if(FETCHCONTENT_SOURCE_DIR_BLOCKFIFO)
  message(STATUS "Using local BlockFIFO: ${FETCHCONTENT_SOURCE_DIR_BLOCKFIFO}")
endif()

# The competitors are vendored
set(CONTENDERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/contenders")

add_subdirectory(wrapper)
add_subdirectory(benchmarks)
add_subdirectory(tools)

if(EXP_BUILD_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()
