cmake_minimum_required(VERSION 3.20.0)

set(ZXSD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
# Preserve modules discovered by west (including board HALs) while also
# allowing this sample to be built from a standalone zxsd checkout.
list(APPEND EXTRA_ZEPHYR_MODULES ${ZXSD_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zxsd_zephyr_rest)

include(${ZXSD_ROOT}/cmake/zxsdGenerate.cmake)

target_sources(app PRIVATE src/main.c)
zxsd_generate(TARGET app SCHEMA slideshow.xsd BINDINGS)
