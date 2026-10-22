cmake_minimum_required(VERSION 3.16)
project(mxsd_host_example LANGUAGES C)

include(CTest)

# Build mxsd from the repository containing this example. Applications using
# a packaged mxsd can replace this with find_package(mxsd CONFIG REQUIRED).
set(MXSD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. mxsd-build)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/mxsdGenerate.cmake)

add_executable(mxsd_example main.c)
mxsd_generate(TARGET mxsd_example SCHEMA sensor.xsd)
target_link_libraries(mxsd_example PRIVATE mxsd::mxsd)
add_test(NAME mxsd_host_example COMMAND mxsd_example)
