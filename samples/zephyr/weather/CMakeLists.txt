# Copyright (c) 2026 Sixtaupi
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(ZXSD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
list(APPEND EXTRA_ZEPHYR_MODULES ${ZXSD_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zxsd_weather)

include(${ZXSD_ROOT}/cmake/zxsdGenerate.cmake)

target_sources(app PRIVATE src/main.c src/weather_summary.c)
zxsd_generate(TARGET app SCHEMA nominatim.xsd BINDINGS)
zxsd_generate(TARGET app SCHEMA met_weather.xsd STREAM_BIND weatherdata/product/time)

