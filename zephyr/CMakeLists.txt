# Copyright (c) 2026 Romain Pelletant
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MXSD)
  set(MXSD_MODULE_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

  zephyr_library()
  zephyr_library_sources(
    ${MXSD_MODULE_ROOT}/src/mxsd.c
    ${MXSD_MODULE_ROOT}/src/mxsd_dom.c
    ${MXSD_MODULE_ROOT}/src/mxsd_identity.c
    ${MXSD_MODULE_ROOT}/src/mxsd_stream.c
    ${MXSD_MODULE_ROOT}/src/mxsd_value.c
    ${MXSD_MODULE_ROOT}/src/mxsd_xml_builtin.c)
  zephyr_library_include_directories(${MXSD_MODULE_ROOT}/src)
  zephyr_include_directories(${MXSD_MODULE_ROOT}/include)
  zephyr_library_compile_definitions(
    MXSD_MAX_INPUT=${CONFIG_MXSD_MAX_INPUT}
    MXSD_MAX_DEPTH=${CONFIG_MXSD_MAX_DEPTH}
    MXSD_MAX_PARTICLES=${CONFIG_MXSD_MAX_PARTICLES}
    MXSD_MAX_ATTRIBUTES=${CONFIG_MXSD_MAX_ATTRIBUTES}
    MXSD_MAX_NAMESPACES=${CONFIG_MXSD_MAX_NAMESPACES}
    MXSD_MAX_IDENTITY_VALUES=${CONFIG_MXSD_MAX_IDENTITY_VALUES}
    MXSD_MAX_TEXT_LENGTH=${CONFIG_MXSD_MAX_TEXT_LENGTH}
    MXSD_STREAM_TEXT_CAPACITY=${CONFIG_MXSD_MAX_TEXT_LENGTH}
    MXSD_MAX_SCHEMA_COMPLEXITY=${CONFIG_MXSD_MAX_SCHEMA_COMPLEXITY})
  if(CONFIG_MXSD_NO_HEAP)
    zephyr_library_compile_definitions(MXSD_NO_HEAP=1)
  endif()
endif()
