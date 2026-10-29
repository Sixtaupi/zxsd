cmake_minimum_required(VERSION 3.16)
project(zxsd_host_example LANGUAGES C)

include(CTest)

# Build zxsd from the repository containing this example. Applications using
# a packaged zxsd can replace this with find_package(zxsd CONFIG REQUIRED).
set(ZXSD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. zxsd-build)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/zxsdGenerate.cmake)

add_executable(zxsd_example main.c)
zxsd_generate(TARGET zxsd_example SCHEMA sensor.xsd)
target_link_libraries(zxsd_example PRIVATE zxsd::zxsd)
add_test(NAME zxsd_host_example COMMAND zxsd_example)
