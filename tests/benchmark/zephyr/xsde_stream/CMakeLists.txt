cmake_minimum_required(VERSION 3.20.0)

set(CONF_FILE ${CMAKE_CURRENT_LIST_DIR}/../../streaming/common.conf
  ${CMAKE_CURRENT_LIST_DIR}/prj.conf)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xsde_streaming_benchmark)

foreach(variable XSDE_COMPILER XSDE_SOURCE_DIR XSDE_BUILD_DIR XSDE_LIBRARY XSDE_RUNTIME_MANIFEST)
  if(NOT DEFINED ${variable} OR NOT EXISTS "${${variable}}")
    message(FATAL_ERROR "Provide an existing ${variable}; see the benchmark README")
  endif()
endforeach()

set(BENCHMARK_IMPLEMENTATION xsde)
include(../../streaming/common.cmake)
configure_file(${XSDE_RUNTIME_MANIFEST} benchmark-runtime.json COPYONLY)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})
add_custom_command(
  OUTPUT ${GENERATED_DIR}/benchmark-pskel.hxx ${GENERATED_DIR}/benchmark-pskel.cxx
  COMMAND ${XSDE_COMPILER} cxx-parser --no-stl --no-iostream --no-exceptions
          --output-dir ${GENERATED_DIR} ${BENCHMARK_ROOT}/benchmark.xsd
  DEPENDS ${BENCHMARK_ROOT}/benchmark.xsd ${XSDE_COMPILER}
  VERBATIM)
target_sources(app PRIVATE ../../streaming/xsde_adapter.cxx
  ${GENERATED_DIR}/benchmark-pskel.cxx)
target_include_directories(app PRIVATE ${GENERATED_DIR} ${XSDE_BUILD_DIR} ${XSDE_SOURCE_DIR})
target_link_libraries(app PRIVATE ${XSDE_LIBRARY})
