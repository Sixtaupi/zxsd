cmake_minimum_required(VERSION 3.20.0)

set(MXSD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../..)
list(APPEND EXTRA_ZEPHYR_MODULES ${MXSD_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mxsd_zephyr_benchmark)

include(${MXSD_ROOT}/cmake/mxsdGenerate.cmake)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(BENCHMARK_ROOT ${MXSD_ROOT}/tests/benchmark)
set(BENCHMARK_SAMPLE_COUNT 1024 CACHE STRING "Number of samples in the benchmark document")
if(NOT BENCHMARK_SAMPLE_COUNT MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "BENCHMARK_SAMPLE_COUNT must be a positive integer")
endif()
target_compile_definitions(app PRIVATE BENCHMARK_SAMPLE_COUNT=${BENCHMARK_SAMPLE_COUNT})
set(BENCHMARK_XML ${CMAKE_CURRENT_BINARY_DIR}/benchmark.xml)
set(BENCHMARK_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/benchmark_fixture.c)
add_custom_command(
  OUTPUT ${BENCHMARK_XML} ${BENCHMARK_FIXTURE}
  COMMAND ${Python3_EXECUTABLE} ${BENCHMARK_ROOT}/generate_fixture.py ${BENCHMARK_XML}
          --samples ${BENCHMARK_SAMPLE_COUNT}
  COMMAND ${Python3_EXECUTABLE} ${BENCHMARK_ROOT}/embed_fixture.py
          ${BENCHMARK_XML} ${BENCHMARK_FIXTURE}
  DEPENDS
    ${BENCHMARK_ROOT}/generate_fixture.py
    ${BENCHMARK_ROOT}/embed_fixture.py
  VERBATIM)

target_sources(app PRIVATE src/main.c ${BENCHMARK_FIXTURE})
option(BENCHMARK_ZEPHYR_CONFIG "Generate schema-specific Kconfig and compile-time checks" OFF)
if(BENCHMARK_ZEPHYR_CONFIG)
  mxsd_generate(TARGET app SCHEMA ${BENCHMARK_ROOT}/benchmark.xsd BINDINGS ZEPHYR_CONFIG)
else()
  mxsd_generate(TARGET app SCHEMA ${BENCHMARK_ROOT}/benchmark.xsd BINDINGS)
endif()
