# Copyright (c) 2026 Sixtaupi
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_ZXSD)
  set(ZXSD_MODULE_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

  zephyr_library()
  zephyr_library_sources(
    ${ZXSD_MODULE_ROOT}/src/zxsd.c
    ${ZXSD_MODULE_ROOT}/src/zxsd_dom.c
    ${ZXSD_MODULE_ROOT}/src/zxsd_identity.c
    ${ZXSD_MODULE_ROOT}/src/zxsd_model.c
    ${ZXSD_MODULE_ROOT}/src/zxsd_regex.c
    ${ZXSD_MODULE_ROOT}/src/zxsd_stream.c
    ${ZXSD_MODULE_ROOT}/src/zxsd_unicode.c
    ${ZXSD_MODULE_ROOT}/src/zxsd_value.c
    ${ZXSD_MODULE_ROOT}/src/zxsd_version.c
    ${ZXSD_MODULE_ROOT}/src/zxsd_xml_stream.c)
  # CONFIG_MXML is provided by a Mini-XML Zephyr module, when one is present.
  # As in host builds, the Mini-XML backend replaces the built-in one and adds
  # the zxsd_mxml_validate() tree adapter.
  if(CONFIG_MXML)
    zephyr_library_sources(${ZXSD_MODULE_ROOT}/src/zxsd_xml_mxml.c)
  else()
    zephyr_library_sources(${ZXSD_MODULE_ROOT}/src/zxsd_xml_builtin.c)
  endif()
  zephyr_library_sources_ifdef(CONFIG_ZXSD_BINDINGS ${ZXSD_MODULE_ROOT}/src/zxsd_bind.c)
  zephyr_library_sources_ifdef(CONFIG_ZXSD_BINDINGS ${ZXSD_MODULE_ROOT}/src/zxsd_bind_stream.c)
  zephyr_library_include_directories(${ZXSD_MODULE_ROOT}/src)
  zephyr_include_directories(${ZXSD_MODULE_ROOT}/include)
  # These limits are part of public structure layouts such as zxsd_stream_t.
  # Keep the runtime, generated sources, and application code on the same ABI.
  zephyr_compile_definitions(
    ZXSD_MAX_INPUT=${CONFIG_ZXSD_MAX_INPUT}
    ZXSD_MAX_DEPTH=${CONFIG_ZXSD_MAX_DEPTH}
    ZXSD_MAX_PARTICLES=${CONFIG_ZXSD_MAX_PARTICLES}
    ZXSD_MAX_ATTRIBUTES=${CONFIG_ZXSD_MAX_ATTRIBUTES}
    ZXSD_MAX_NAMESPACES=${CONFIG_ZXSD_MAX_NAMESPACES}
    ZXSD_MAX_IDENTITY_VALUES=${CONFIG_ZXSD_MAX_IDENTITY_VALUES}
    ZXSD_MAX_TEXT_LENGTH=${CONFIG_ZXSD_MAX_TEXT_LENGTH}
    ZXSD_STREAM_CHILD_CAPACITY=${CONFIG_ZXSD_STREAM_CHILD_CAPACITY}
    ZXSD_STREAM_MODEL_CAPACITY=${CONFIG_ZXSD_STREAM_MODEL_CAPACITY}
    ZXSD_STREAM_NAME_CAPACITY=${CONFIG_ZXSD_STREAM_NAME_CAPACITY}
    ZXSD_MAX_PATTERN_LENGTH=${CONFIG_ZXSD_MAX_PATTERN_LENGTH}
    ZXSD_MAX_REGEX_NODES=${CONFIG_ZXSD_MAX_REGEX_NODES}
    ZXSD_MAX_REGEX_DEPTH=${CONFIG_ZXSD_MAX_REGEX_DEPTH}
    ZXSD_STREAM_TEXT_CAPACITY=${CONFIG_ZXSD_MAX_TEXT_LENGTH}
    ZXSD_XML_TOKEN_CAPACITY=${CONFIG_ZXSD_XML_TOKEN_CAPACITY}
    ZXSD_MAX_SCHEMA_COMPLEXITY=${CONFIG_ZXSD_MAX_SCHEMA_COMPLEXITY})
  if(CONFIG_ZXSD_NO_HEAP)
    zephyr_compile_definitions(ZXSD_NO_HEAP=1)
  endif()
endif()
