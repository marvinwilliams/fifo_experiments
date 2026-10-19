set_directory_properties(PROPERTIES EXCLUDE_FROM_ALL TRUE)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  set(EXP_ARCH_X86_64 ON)
else()
  set(EXP_ARCH_X86_64 OFF)
endif()
message(STATUS "Target architecture: ${CMAKE_SYSTEM_PROCESSOR}")

add_library(bf INTERFACE)
target_link_libraries(bf INTERFACE blockfifo::blockfifo)
target_compile_definitions(bf INTERFACE FIFO_BLOCKFIFO)

set(EXP_MULTIFIFO_CLOCK
    "cycle"
    CACHE STRING "Clock for the MultiFIFO timestamps (cycle: rdtsc / cntvct_el0, steady: std::chrono::steady_clock)")
set_property(CACHE EXP_MULTIFIFO_CLOCK PROPERTY STRINGS cycle steady)

add_library(mf INTERFACE)
target_link_libraries(mf INTERFACE multififo::multififo)

target_compile_definitions(mf INTERFACE FIFO_MULTIFIFO)

if(EXP_MULTIFIFO_CLOCK STREQUAL "steady")
  target_compile_definitions(mf INTERFACE MULTIFIFO_STEADY_CLOCK)
elseif(NOT EXP_MULTIFIFO_CLOCK STREQUAL "cycle")
  message(FATAL_ERROR "Invalid EXP_MULTIFIFO_CLOCK '${EXP_MULTIFIFO_CLOCK}' (cycle, steady)")
endif()

add_library(mf_random INTERFACE)
target_link_libraries(mf_random INTERFACE mf)
target_compile_definitions(mf_random INTERFACE MF_MODE_RANDOM)

add_library(mf_stick_swap INTERFACE)
target_link_libraries(mf_stick_swap INTERFACE mf)
target_compile_definitions(mf_stick_swap INTERFACE MF_MODE_STICK_SWAP)

add_library(kfifo INTERFACE)
target_include_directories(kfifo SYSTEM INTERFACE ${CONTENDERS_DIR})
target_compile_definitions(kfifo INTERFACE FIFO_KFIFO)

add_library(faaaqueue INTERFACE)
target_include_directories(faaaqueue SYSTEM INTERFACE ${CONTENDERS_DIR})
target_compile_definitions(faaaqueue INTERFACE FIFO_FAAAQUEUE)

set(DCBO_SOURCE_DIR "${CONTENDERS_DIR}/2D")
add_library(dcbo_c STATIC ${DCBO_SOURCE_DIR}/d-balanced-queue.c ${DCBO_SOURCE_DIR}/lcrq.c ${DCBO_SOURCE_DIR}/ssalloc.c
                          ${CMAKE_CURRENT_SOURCE_DIR}/dcbo/dcbo_shim.c)
target_include_directories(dcbo_c PRIVATE ${DCBO_SOURCE_DIR})
target_include_directories(dcbo_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dcbo)
target_compile_definitions(dcbo_c PRIVATE _GNU_SOURCE)
target_compile_options(dcbo_c PRIVATE -w)
target_link_libraries(dcbo_c PUBLIC atomic Threads::Threads)
if(EXP_ARCH_X86_64)
  target_compile_options(dcbo_c PRIVATE -mcx16)
endif()

add_library(dcbo INTERFACE)
target_link_libraries(dcbo INTERFACE dcbo_c)
target_compile_definitions(dcbo INTERFACE FIFO_DCBO)

add_library(locked_fifo INTERFACE)
target_compile_definitions(locked_fifo INTERFACE FIFO_LOCKED_FIFO)

set(COMPETITORS
    bf
    mf_random
    mf_stick_swap
    kfifo
    faaaqueue
    dcbo
    locked_fifo)

if(EXP_ARCH_X86_64)
  set(x86_default ON)
else()
  set(x86_default OFF)
endif()
option(EXP_X86_COMPETITORS "Build the x86-64-only competitors (LCRQ)" ${x86_default})
if(EXP_X86_COMPETITORS AND NOT EXP_ARCH_X86_64)
  message(FATAL_ERROR "EXP_X86_COMPETITORS requires an x86-64 target")
endif()

if(EXP_X86_COMPETITORS)
  add_library(lcrq INTERFACE)
  target_include_directories(lcrq SYSTEM INTERFACE ${CONTENDERS_DIR})
  target_compile_definitions(lcrq INTERFACE FIFO_LCRQ)
  target_compile_options(lcrq INTERFACE -mcx16)
  target_link_libraries(lcrq INTERFACE atomic)
  list(APPEND COMPETITORS lcrq)
else()
  message(STATUS "Not building the x86-64-only competitors (LCRQ)")
endif()

find_package(TBB)

if(TBB_FOUND)
  add_library(tbb_fifo INTERFACE)
  target_link_libraries(tbb_fifo INTERFACE TBB::tbb)
  target_compile_definitions(tbb_fifo INTERFACE FIFO_TBB_FIFO)
  list(APPEND COMPETITORS tbb_fifo)
else()
  message(WARNING "TBB not found, not building TBB FIFO")
endif()

message(STATUS "Competitors: ${COMPETITORS}")

set(COMPETITORS
    ${COMPETITORS}
    PARENT_SCOPE)
