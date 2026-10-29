cmake_minimum_required(VERSION 3.20.0)

set(ZXSD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
list(APPEND EXTRA_ZEPHYR_MODULES ${ZXSD_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zxsd_model_limits_test)

target_sources(app PRIVATE src/main.c ${ZXSD_ROOT}/tests/test_model_limits.c)
target_include_directories(app PRIVATE ${ZXSD_ROOT}/src)
target_compile_definitions(app PRIVATE ZXSD_TEST_NO_MAIN=1)
