cmake_minimum_required(VERSION 3.20.0)

set(MXSD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
list(APPEND EXTRA_ZEPHYR_MODULES ${MXSD_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mxsd_model_limits_test)

target_sources(app PRIVATE src/main.c ${MXSD_ROOT}/tests/test_model_limits.c)
target_include_directories(app PRIVATE ${MXSD_ROOT}/src)
target_compile_definitions(app PRIVATE MXSD_TEST_NO_MAIN=1)
