cmake_minimum_required(VERSION 3.20.0)

set(MXSD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../..)
list(APPEND EXTRA_ZEPHYR_MODULES ${MXSD_ROOT})
set(CONF_FILE ${CMAKE_CURRENT_LIST_DIR}/../../streaming/common.conf
  ${CMAKE_CURRENT_LIST_DIR}/prj.conf)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mxsd_streaming_benchmark)

set(BENCHMARK_IMPLEMENTATION mxsd)
include(../../streaming/common.cmake)
include(${MXSD_ROOT}/cmake/mxsdGenerate.cmake)
target_sources(app PRIVATE ../../streaming/mxsd_adapter.c)
mxsd_generate(TARGET app SCHEMA ${BENCHMARK_ROOT}/benchmark.xsd STREAM_BIND samples/sample)
