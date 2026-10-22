cmake_minimum_required(VERSION 3.20.0)

set(MXSD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
list(APPEND EXTRA_ZEPHYR_MODULES ${MXSD_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mxsd_zephyr_validation_test)

include(${MXSD_ROOT}/cmake/mxsdGenerate.cmake)

set(MXSD_TEST_ROOT ${MXSD_ROOT}/tests)
target_sources(app PRIVATE
  src/main.c
  ${MXSD_TEST_ROOT}/test_runtime.c
  ${MXSD_TEST_ROOT}/test_advanced.c
  ${MXSD_TEST_ROOT}/test_derivation.c
  ${MXSD_TEST_ROOT}/test_simple_types.c
  ${MXSD_TEST_ROOT}/test_wildcards.c
  ${MXSD_TEST_ROOT}/test_identity.c
  ${MXSD_TEST_ROOT}/test_composition.c
  ${MXSD_TEST_ROOT}/test_element_semantics.c
  ${MXSD_TEST_ROOT}/test_groups.c
  ${MXSD_TEST_ROOT}/test_mixed_any.c
  ${MXSD_TEST_ROOT}/test_recursive.c
  ${MXSD_TEST_ROOT}/test_regex.c
  ${MXSD_TEST_ROOT}/test_notation.c
  ${MXSD_TEST_ROOT}/test_declarations.c
  ${MXSD_TEST_ROOT}/test_bindings.c)
target_compile_definitions(app PRIVATE MXSD_TEST_NO_MAIN=1)

mxsd_generate(TARGET app SCHEMA ${MXSD_TEST_ROOT}/device.xsd)
mxsd_generate(TARGET app SCHEMA ${MXSD_TEST_ROOT}/advanced.xsd)
mxsd_generate(TARGET app SCHEMA ${MXSD_TEST_ROOT}/derivation.xsd)
mxsd_generate(TARGET app SCHEMA ${MXSD_TEST_ROOT}/simple_types.xsd)
mxsd_generate(TARGET app SCHEMA ${MXSD_TEST_ROOT}/wildcards.xsd)
mxsd_generate(TARGET app SCHEMA ${MXSD_TEST_ROOT}/identity.xsd)
mxsd_generate(
  TARGET app
  SCHEMA ${MXSD_TEST_ROOT}/composition.xsd
  DEPENDS
    ${MXSD_TEST_ROOT}/composition_include.xsd
    ${MXSD_TEST_ROOT}/composition_foreign.xsd
    ${MXSD_TEST_ROOT}/composition_base.xsd)
mxsd_generate(TARGET app SCHEMA ${MXSD_TEST_ROOT}/element_semantics.xsd)
mxsd_generate(TARGET app SCHEMA ${MXSD_TEST_ROOT}/groups.xsd)
mxsd_generate(TARGET app SCHEMA ${MXSD_TEST_ROOT}/mixed_any.xsd)
mxsd_generate(TARGET app SCHEMA ${MXSD_TEST_ROOT}/recursive.xsd)
mxsd_generate(TARGET app SCHEMA ${MXSD_TEST_ROOT}/regex.xsd)
mxsd_generate(
  TARGET app
  SCHEMA ${MXSD_TEST_ROOT}/notation.xsd
  DEPENDS ${MXSD_TEST_ROOT}/notation_formats.xsd)
mxsd_generate(TARGET app SCHEMA ${MXSD_TEST_ROOT}/declarations.xsd)
mxsd_generate(TARGET app SCHEMA ${MXSD_TEST_ROOT}/bindings.xsd BINDINGS)
