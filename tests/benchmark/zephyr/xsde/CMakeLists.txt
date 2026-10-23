cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xsde_zephyr_benchmark)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

foreach(variable XSDE_COMPILER XSDE_SOURCE_DIR XSDE_BUILD_DIR XSDE_LIBRARY)
  if(NOT DEFINED ${variable})
    message(FATAL_ERROR "${variable} must be provided on the west build command line")
  endif()
endforeach()

set(BENCHMARK_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

execute_process(
  COMMAND
    ${XSDE_COMPILER} cxx-parser --no-stl --no-iostream --no-exceptions
    --generate-noop-impl --output-dir ${GENERATED_DIR}
    ${BENCHMARK_ROOT}/benchmark.xsd
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(
  COMMAND
    ${XSDE_COMPILER} cxx-serializer --no-stl --no-iostream --no-exceptions
    --output-dir ${GENERATED_DIR} ${BENCHMARK_ROOT}/benchmark.xsd
  COMMAND_ERROR_IS_FATAL ANY)

set(BENCHMARK_XML ${CMAKE_CURRENT_BINARY_DIR}/benchmark.xml)
set(BENCHMARK_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/benchmark_fixture.c)
add_custom_command(
  OUTPUT ${BENCHMARK_XML} ${BENCHMARK_FIXTURE}
  COMMAND ${Python3_EXECUTABLE} ${BENCHMARK_ROOT}/generate_fixture.py ${BENCHMARK_XML}
  COMMAND ${Python3_EXECUTABLE} ${BENCHMARK_ROOT}/embed_fixture.py
          ${BENCHMARK_XML} ${BENCHMARK_FIXTURE}
  DEPENDS
    ${BENCHMARK_ROOT}/generate_fixture.py
    ${BENCHMARK_ROOT}/embed_fixture.py
  VERBATIM)

target_sources(
  app PRIVATE
  src/main.cpp
  ${BENCHMARK_FIXTURE}
  ${GENERATED_DIR}/benchmark-pimpl.cxx
  ${GENERATED_DIR}/benchmark-pskel.cxx
  ${GENERATED_DIR}/benchmark-sskel.cxx)
target_include_directories(
  app PRIVATE ${GENERATED_DIR} ${XSDE_BUILD_DIR} ${XSDE_SOURCE_DIR})
target_link_libraries(app PRIVATE ${XSDE_LIBRARY})

