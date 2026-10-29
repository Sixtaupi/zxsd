# Copyright (c) 2026 Sixtaupi
# SPDX-License-Identifier: Apache-2.0

include_guard()

include(CMakeParseArguments)

# Keep the source-tree compiler discoverable when this module is included
# directly. An installed package can set ZXSDC_EXECUTABLE before including it.
get_filename_component(_ZXSD_GENERATE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED ZXSDC_EXECUTABLE AND EXISTS "${_ZXSD_GENERATE_ROOT}/zxsdc")
  set(ZXSDC_EXECUTABLE "${_ZXSD_GENERATE_ROOT}/zxsdc")
endif()

#[=======================================================================[.rst:
zxsd_generate
-------------

Generate and compile an zxsd validator as part of an existing CMake target::

  zxsd_generate(
    TARGET application
    SCHEMA device.xsd
    [BINDINGS]
    [ZEPHYR_CONFIG]
    [BIND_ROOT name ...]
    [STREAM_BIND element/path ...]
    [SCHEMA_PATH directory ...]
    [DEPENDS schema-file ...]
    [MAX_SCHEMA_COMPLEXITY count]
    [OUTPUT_DIR directory]
    [ALLOW_UNRESOLVED_SCHEMA_LOCATIONS])

The schema compiler always runs on the build host. Generated sources and the
generated include directory are added privately to ``TARGET``. ``DEPENDS``
must list locally included, imported, or redefined schemas whose modification
must trigger regeneration.

``STREAM_BIND`` resolves each finite absolute element path on the host and
generates typed fragment readers in ``<stem>.bind.[ch]``. It does not require
``BINDINGS``; that option separately enables full-document object readers and
writers. Repeated paths generate independent reader types.

``ZEPHYR_CONFIG`` additionally generates a ``<stem>.conf`` recommendation file
and ``<stem>.limits.h`` checks. Generate and review the fragment on the host
before Zephyr configuration; this build-time helper does not merge it into
Kconfig, whose evaluation has already taken place.
#]=======================================================================]
function(zxsd_generate)
  set(options BINDINGS ZEPHYR_CONFIG ALLOW_UNRESOLVED_SCHEMA_LOCATIONS)
  set(one_value_args TARGET SCHEMA OUTPUT_DIR MAX_SCHEMA_COMPLEXITY)
  set(multi_value_args BIND_ROOT STREAM_BIND SCHEMA_PATH DEPENDS)
  cmake_parse_arguments(ZXSD "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(ZXSD_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "zxsd_generate: unknown arguments: ${ZXSD_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT ZXSD_TARGET)
    message(FATAL_ERROR "zxsd_generate: TARGET is required")
  endif()
  if(NOT TARGET "${ZXSD_TARGET}")
    message(FATAL_ERROR "zxsd_generate: target '${ZXSD_TARGET}' does not exist")
  endif()
  if(NOT ZXSD_SCHEMA)
    message(FATAL_ERROR "zxsd_generate: SCHEMA is required")
  endif()
  if(ZXSD_BIND_ROOT AND NOT ZXSD_BINDINGS)
    message(FATAL_ERROR "zxsd_generate: BIND_ROOT requires BINDINGS")
  endif()

  if(IS_ABSOLUTE "${ZXSD_SCHEMA}")
    set(schema "${ZXSD_SCHEMA}")
  else()
    get_filename_component(schema "${ZXSD_SCHEMA}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  if(NOT EXISTS "${schema}")
    message(FATAL_ERROR "zxsd_generate: schema does not exist: ${schema}")
  endif()

  if(NOT DEFINED ZXSDC_EXECUTABLE OR NOT EXISTS "${ZXSDC_EXECUTABLE}")
    find_program(ZXSDC_EXECUTABLE NAMES zxsdc DOC "zxsd host schema compiler")
  endif()
  if(NOT ZXSDC_EXECUTABLE)
    message(FATAL_ERROR "zxsd_generate: zxsdc was not found; set ZXSDC_EXECUTABLE")
  endif()
  if(NOT Python3_EXECUTABLE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
  endif()

  get_filename_component(schema_stem "${schema}" NAME_WE)
  string(MAKE_C_IDENTIFIER "${schema_stem}" stem)
  if(ZXSD_OUTPUT_DIR)
    if(IS_ABSOLUTE "${ZXSD_OUTPUT_DIR}")
      set(output_dir "${ZXSD_OUTPUT_DIR}")
    else()
      get_filename_component(output_dir "${ZXSD_OUTPUT_DIR}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
  else()
    set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/zxsd-generated/${ZXSD_TARGET}/${stem}")
  endif()

  set(outputs
    "${output_dir}/${stem}.zxsd.c"
    "${output_dir}/${stem}.zxsd.h")
  set(generated_sources "${output_dir}/${stem}.zxsd.c")
  if(ZXSD_BINDINGS OR ZXSD_STREAM_BIND)
    list(APPEND outputs
      "${output_dir}/${stem}.bind.c"
      "${output_dir}/${stem}.bind.h")
    list(APPEND generated_sources "${output_dir}/${stem}.bind.c")
  endif()

  set(command_args "${schema}" -o "${output_dir}")
  if(ZXSD_ZEPHYR_CONFIG)
    list(APPEND outputs "${output_dir}/${stem}.conf" "${output_dir}/${stem}.limits.h")
    list(APPEND command_args --zephyr-config)
  endif()
  foreach(path IN LISTS ZXSD_SCHEMA_PATH)
    if(NOT IS_ABSOLUTE "${path}")
      get_filename_component(path "${path}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    list(APPEND command_args -I "${path}")
  endforeach()
  if(ZXSD_MAX_SCHEMA_COMPLEXITY)
    list(APPEND command_args --max-schema-complexity "${ZXSD_MAX_SCHEMA_COMPLEXITY}")
  endif()
  if(ZXSD_ALLOW_UNRESOLVED_SCHEMA_LOCATIONS)
    list(APPEND command_args --allow-unresolved-schema-locations)
  endif()
  if(ZXSD_BINDINGS)
    list(APPEND command_args --bindings)
  endif()
  foreach(path IN LISTS ZXSD_STREAM_BIND)
    list(APPEND command_args --stream-bind "${path}")
  endforeach()
  foreach(root IN LISTS ZXSD_BIND_ROOT)
    list(APPEND command_args --bind-root "${root}")
  endforeach()

  get_filename_component(zxsdc_directory "${ZXSDC_EXECUTABLE}" DIRECTORY)
  file(GLOB zxsdc_modules CONFIGURE_DEPENDS "${zxsdc_directory}/zxsdc_lib/*.py")
  set(dependencies "${schema}" "${ZXSDC_EXECUTABLE}" ${zxsdc_modules})
  foreach(dependency IN LISTS ZXSD_DEPENDS)
    if(NOT IS_ABSOLUTE "${dependency}")
      get_filename_component(dependency "${dependency}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    list(APPEND dependencies "${dependency}")
  endforeach()

  add_custom_command(
    OUTPUT ${outputs}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${output_dir}"
    COMMAND "${Python3_EXECUTABLE}" "${ZXSDC_EXECUTABLE}" ${command_args}
    DEPENDS ${dependencies}
    COMMENT "Generating zxsd validator for ${schema}"
    VERBATIM)

  set_source_files_properties(${outputs} PROPERTIES GENERATED TRUE)
  target_sources("${ZXSD_TARGET}" PRIVATE ${generated_sources})
  target_include_directories("${ZXSD_TARGET}" PRIVATE "${output_dir}")

  set(${ZXSD_TARGET}_ZXSD_OUTPUTS "${outputs}" PARENT_SCOPE)
  set(${ZXSD_TARGET}_ZXSD_INCLUDE_DIR "${output_dir}" PARENT_SCOPE)
endfunction()
