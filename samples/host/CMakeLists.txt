cmake_minimum_required(VERSION 3.16)
project(mxsd_host_example LANGUAGES C)

include(CTest)

# Build mxsd from the repository containing this example. Applications using
# a packaged mxsd can replace this with find_package(mxsd CONFIG REQUIRED).
set(MXSD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. mxsd-build)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(GENERATED_C ${GENERATED_DIR}/sensor.mxsd.c)
set(GENERATED_H ${GENERATED_DIR}/sensor.mxsd.h)

add_custom_command(
  OUTPUT ${GENERATED_C} ${GENERATED_H}
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_LIST_DIR}/../../mxsdc
          ${CMAKE_CURRENT_LIST_DIR}/sensor.xsd
          -o ${GENERATED_DIR}
  DEPENDS
    ${CMAKE_CURRENT_LIST_DIR}/../../mxsdc
    ${CMAKE_CURRENT_LIST_DIR}/sensor.xsd
  COMMENT "Generating the sensor XML validator"
  VERBATIM)

add_executable(mxsd_example main.c ${GENERATED_C})
target_include_directories(mxsd_example PRIVATE ${GENERATED_DIR})
target_link_libraries(mxsd_example PRIVATE mxsd::mxsd)
add_test(NAME mxsd_host_example COMMAND mxsd_example)
