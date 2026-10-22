cmake_minimum_required(VERSION 3.20.0)

set(MXSD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
# Preserve modules discovered by west (including board HALs) while also
# allowing this sample to be built from a standalone mxsd checkout.
list(APPEND EXTRA_ZEPHYR_MODULES ${MXSD_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mxsd_zephyr_rest)

include(${MXSD_ROOT}/cmake/mxsdGenerate.cmake)

target_sources(app PRIVATE src/main.c)
mxsd_generate(TARGET app SCHEMA slideshow.xsd BINDINGS)
