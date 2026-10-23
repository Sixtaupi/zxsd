# SPDX-License-Identifier: Apache-2.0
# Included by both Zephyr applications after find_package(Zephyr).

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BENCHMARK_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(BENCHMARK_SAMPLE_COUNT 1024 CACHE STRING "Samples in the common fixture")
set(BENCHMARK_BLOCK_SIZE 4096 CACHE STRING "Bytes per feed for both implementations")
set(BENCHMARK_RUNS 20 CACHE STRING "Measured iterations after one warm-up")
foreach(option BENCHMARK_SAMPLE_COUNT BENCHMARK_BLOCK_SIZE BENCHMARK_RUNS)
  if(NOT ${option} MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "${option} must be a positive integer")
  endif()
endforeach()

# Apply equally to application, generated code, and the mxsd runtime.
# The external XSD/e runtime is built with build_stream_runtime.py and audited by run_streaming.py.
zephyr_compile_definitions(NDEBUG)
target_compile_definitions(app PRIVATE
  BENCHMARK_IMPLEMENTATION="${BENCHMARK_IMPLEMENTATION}"
  BENCHMARK_RUNS=${BENCHMARK_RUNS}
  BENCHMARK_BLOCK_SIZE=${BENCHMARK_BLOCK_SIZE})
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR})

set(BENCHMARK_XML ${CMAKE_CURRENT_BINARY_DIR}/benchmark.xml)
set(BENCHMARK_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/benchmark_fixture.c)
add_custom_command(
  OUTPUT ${BENCHMARK_XML} ${BENCHMARK_FIXTURE}
  COMMAND ${Python3_EXECUTABLE} ${BENCHMARK_ROOT}/generate_fixture.py ${BENCHMARK_XML}
          --samples ${BENCHMARK_SAMPLE_COUNT}
  COMMAND ${Python3_EXECUTABLE} ${BENCHMARK_ROOT}/embed_fixture.py
          ${BENCHMARK_XML} ${BENCHMARK_FIXTURE}
  DEPENDS ${BENCHMARK_ROOT}/generate_fixture.py ${BENCHMARK_ROOT}/embed_fixture.py
  VERBATIM)
target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/zephyr_main.c
  ${CMAKE_CURRENT_LIST_DIR}/observer.c
  ${CMAKE_CURRENT_LIST_DIR}/selftest.c
  ${BENCHMARK_FIXTURE})
