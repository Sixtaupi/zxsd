cmake_minimum_required(VERSION 3.20.0)

set(ZXSD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
list(APPEND EXTRA_ZEPHYR_MODULES ${ZXSD_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zxsd_zephyr_validation_test)

include(${ZXSD_ROOT}/cmake/zxsdGenerate.cmake)

set(ZXSD_TEST_ROOT ${ZXSD_ROOT}/tests)
target_sources(app PRIVATE
  src/main.c
  ${ZXSD_TEST_ROOT}/test_compatibility.c
  ${ZXSD_TEST_ROOT}/test_runtime.c
  ${ZXSD_TEST_ROOT}/test_advanced.c
  ${ZXSD_TEST_ROOT}/test_derivation.c
  ${ZXSD_TEST_ROOT}/test_simple_types.c
  ${ZXSD_TEST_ROOT}/test_wildcards.c
  ${ZXSD_TEST_ROOT}/test_identity.c
  ${ZXSD_TEST_ROOT}/test_composition.c
  ${ZXSD_TEST_ROOT}/test_element_semantics.c
  ${ZXSD_TEST_ROOT}/test_groups.c
  ${ZXSD_TEST_ROOT}/test_stream_models.c
  ${ZXSD_TEST_ROOT}/test_mixed_any.c
  ${ZXSD_TEST_ROOT}/test_recursive.c
  ${ZXSD_TEST_ROOT}/test_regex.c
  ${ZXSD_TEST_ROOT}/test_notation.c
  ${ZXSD_TEST_ROOT}/test_declarations.c
  ${ZXSD_TEST_ROOT}/test_bindings.c
  ${ZXSD_TEST_ROOT}/test_stream_bindings.c
  ${ZXSD_TEST_ROOT}/test_weather_binding.c
  ${ZXSD_ROOT}/samples/zephyr/weather/src/weather_summary.c)
target_include_directories(app PRIVATE ${ZXSD_ROOT}/samples/zephyr/weather/src)
target_compile_definitions(app PRIVATE ZXSD_TEST_NO_MAIN=1)

zxsd_generate(TARGET app SCHEMA ${ZXSD_TEST_ROOT}/device.xsd)
zxsd_generate(TARGET app SCHEMA ${ZXSD_TEST_ROOT}/advanced.xsd)
zxsd_generate(TARGET app SCHEMA ${ZXSD_TEST_ROOT}/derivation.xsd)
zxsd_generate(TARGET app SCHEMA ${ZXSD_TEST_ROOT}/simple_types.xsd)
zxsd_generate(TARGET app SCHEMA ${ZXSD_TEST_ROOT}/wildcards.xsd)
zxsd_generate(TARGET app SCHEMA ${ZXSD_TEST_ROOT}/identity.xsd)
zxsd_generate(
  TARGET app
  SCHEMA ${ZXSD_TEST_ROOT}/composition.xsd
  DEPENDS
    ${ZXSD_TEST_ROOT}/composition_include.xsd
    ${ZXSD_TEST_ROOT}/composition_foreign.xsd
    ${ZXSD_TEST_ROOT}/composition_base.xsd)
zxsd_generate(TARGET app SCHEMA ${ZXSD_TEST_ROOT}/element_semantics.xsd)
zxsd_generate(TARGET app SCHEMA ${ZXSD_TEST_ROOT}/groups.xsd)
zxsd_generate(TARGET app SCHEMA ${ZXSD_TEST_ROOT}/stream_models.xsd)
zxsd_generate(TARGET app SCHEMA ${ZXSD_TEST_ROOT}/mixed_any.xsd)
zxsd_generate(TARGET app SCHEMA ${ZXSD_TEST_ROOT}/recursive.xsd)
zxsd_generate(TARGET app SCHEMA ${ZXSD_TEST_ROOT}/regex.xsd)
zxsd_generate(
  TARGET app
  SCHEMA ${ZXSD_TEST_ROOT}/notation.xsd
  DEPENDS ${ZXSD_TEST_ROOT}/notation_formats.xsd)
zxsd_generate(TARGET app SCHEMA ${ZXSD_TEST_ROOT}/declarations.xsd)
zxsd_generate(TARGET app SCHEMA ${ZXSD_TEST_ROOT}/bindings.xsd BINDINGS)
zxsd_generate(TARGET app SCHEMA ${ZXSD_TEST_ROOT}/stream_bindings.xsd
  BINDINGS BIND_ROOT row STREAM_BIND doc/row doc/row/label row uniqueDoc/row)
zxsd_generate(TARGET app SCHEMA ${ZXSD_ROOT}/samples/zephyr/weather/met_weather.xsd
  STREAM_BIND weatherdata/product/time)
