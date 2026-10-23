cmake_minimum_required(VERSION 3.16)
project(mxsd_xsde_benchmark LANGUAGES C)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

get_filename_component(MXSD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(MXSDC_EXECUTABLE "${MXSD_SOURCE_DIR}/mxsdc" CACHE FILEPATH "" FORCE)
set(MXSD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MXSD_USE_MXML OFF CACHE BOOL "" FORCE)
set(MXSD_NO_HEAP ON CACHE BOOL "" FORCE)
add_subdirectory("${MXSD_SOURCE_DIR}" mxsd-project)

# Flat models use incremental counters even when their repetition count exceeds
# the history capacity. The MET Norway schema needs 45 distinct child particles.
target_compile_definitions(mxsd PUBLIC
  MXSD_MAX_DEPTH=8
  MXSD_MAX_PARTICLES=64
  MXSD_MAX_TEXT_LENGTH=64
  MXSD_STREAM_TEXT_CAPACITY=64
  MXSD_STREAM_CHILD_CAPACITY=1)

add_executable(mxsd-benchmark mxsd_runner.c)
target_link_libraries(mxsd-benchmark PRIVATE mxsd)
mxsd_generate(TARGET mxsd-benchmark SCHEMA benchmark.xsd)

add_executable(mxsd-serializer-benchmark mxsd_serializer.c)
target_link_libraries(mxsd-serializer-benchmark PRIVATE mxsd)
mxsd_generate(TARGET mxsd-serializer-benchmark SCHEMA benchmark.xsd BINDINGS)

add_executable(mxsd-weather-stream-check weather_stream_check.c)
target_link_libraries(mxsd-weather-stream-check PRIVATE mxsd)
mxsd_generate(TARGET mxsd-weather-stream-check
  SCHEMA ${MXSD_SOURCE_DIR}/samples/zephyr/weather/met_weather.xsd)
enable_testing()
add_test(NAME weather-stream-small-history COMMAND mxsd-weather-stream-check)

add_executable(mxsd-stream-binding-check
  streaming/host_check.c streaming/selftest.c streaming/observer.c streaming/mxsd_adapter.c)
target_include_directories(mxsd-stream-binding-check PRIVATE streaming)
target_link_libraries(mxsd-stream-binding-check PRIVATE mxsd)
mxsd_generate(TARGET mxsd-stream-binding-check SCHEMA benchmark.xsd STREAM_BIND samples/sample)
add_test(NAME typed-stream-binding-contract COMMAND mxsd-stream-binding-check)
add_test(NAME benchmark-report-protocol
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../test_benchmark.py)

set(BENCHMARK_XML "${CMAKE_CURRENT_BINARY_DIR}/benchmark.xml")
add_custom_command(
  OUTPUT "${BENCHMARK_XML}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_fixture.py"
          "${BENCHMARK_XML}"
  DEPENDS generate_fixture.py
  COMMENT "Generating the deterministic benchmark XML fixture"
  VERBATIM)
add_custom_target(benchmark-fixture ALL DEPENDS "${BENCHMARK_XML}")
