# Copyright (c) 2026 Romain Pelletant
# SPDX-License-Identifier: Apache-2.0

include_guard()

include(CMakeParseArguments)

# Keep the source-tree compiler discoverable when this module is included
# directly. An installed package can set MXSDC_EXECUTABLE before including it.
get_filename_component(_MXSD_GENERATE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED MXSDC_EXECUTABLE AND EXISTS "${_MXSD_GENERATE_ROOT}/mxsdc")
  set(MXSDC_EXECUTABLE "${_MXSD_GENERATE_ROOT}/mxsdc")
endif()

#[=======================================================================[.rst:
mxsd_generate
-------------

Generate and compile an mxsd validator as part of an existing CMake target::

  mxsd_generate(
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
function(mxsd_generate)
  set(options BINDINGS ZEPHYR_CONFIG ALLOW_UNRESOLVED_SCHEMA_LOCATIONS)
  set(one_value_args TARGET SCHEMA OUTPUT_DIR MAX_SCHEMA_COMPLEXITY)
  set(multi_value_args BIND_ROOT STREAM_BIND SCHEMA_PATH DEPENDS)
  cmake_parse_arguments(MXSD "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(MXSD_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "mxsd_generate: unknown arguments: ${MXSD_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT MXSD_TARGET)
    message(FATAL_ERROR "mxsd_generate: TARGET is required")
  endif()
  if(NOT TARGET "${MXSD_TARGET}")
    message(FATAL_ERROR "mxsd_generate: target '${MXSD_TARGET}' does not exist")
  endif()
  if(NOT MXSD_SCHEMA)
    message(FATAL_ERROR "mxsd_generate: SCHEMA is required")
  endif()
  if(MXSD_BIND_ROOT AND NOT MXSD_BINDINGS)
    message(FATAL_ERROR "mxsd_generate: BIND_ROOT requires BINDINGS")
  endif()

  if(IS_ABSOLUTE "${MXSD_SCHEMA}")
    set(schema "${MXSD_SCHEMA}")
  else()
    get_filename_component(schema "${MXSD_SCHEMA}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  if(NOT EXISTS "${schema}")
    message(FATAL_ERROR "mxsd_generate: schema does not exist: ${schema}")
  endif()

  if(NOT DEFINED MXSDC_EXECUTABLE OR NOT EXISTS "${MXSDC_EXECUTABLE}")
    find_program(MXSDC_EXECUTABLE NAMES mxsdc DOC "mxsd host schema compiler")
  endif()
  if(NOT MXSDC_EXECUTABLE)
    message(FATAL_ERROR "mxsd_generate: mxsdc was not found; set MXSDC_EXECUTABLE")
  endif()
  if(NOT Python3_EXECUTABLE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
  endif()

  get_filename_component(schema_stem "${schema}" NAME_WE)
  string(MAKE_C_IDENTIFIER "${schema_stem}" stem)
  if(MXSD_OUTPUT_DIR)
    if(IS_ABSOLUTE "${MXSD_OUTPUT_DIR}")
      set(output_dir "${MXSD_OUTPUT_DIR}")
    else()
      get_filename_component(output_dir "${MXSD_OUTPUT_DIR}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
  else()
    set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/mxsd-generated/${MXSD_TARGET}/${stem}")
  endif()

  set(outputs
    "${output_dir}/${stem}.mxsd.c"
    "${output_dir}/${stem}.mxsd.h")
  set(generated_sources "${output_dir}/${stem}.mxsd.c")
  if(MXSD_BINDINGS OR MXSD_STREAM_BIND)
    list(APPEND outputs
      "${output_dir}/${stem}.bind.c"
      "${output_dir}/${stem}.bind.h")
    list(APPEND generated_sources "${output_dir}/${stem}.bind.c")
  endif()

  set(command_args "${schema}" -o "${output_dir}")
  if(MXSD_ZEPHYR_CONFIG)
    list(APPEND outputs "${output_dir}/${stem}.conf" "${output_dir}/${stem}.limits.h")
    list(APPEND command_args --zephyr-config)
  endif()
  foreach(path IN LISTS MXSD_SCHEMA_PATH)
    if(NOT IS_ABSOLUTE "${path}")
      get_filename_component(path "${path}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    list(APPEND command_args -I "${path}")
  endforeach()
  if(MXSD_MAX_SCHEMA_COMPLEXITY)
    list(APPEND command_args --max-schema-complexity "${MXSD_MAX_SCHEMA_COMPLEXITY}")
  endif()
  if(MXSD_ALLOW_UNRESOLVED_SCHEMA_LOCATIONS)
    list(APPEND command_args --allow-unresolved-schema-locations)
  endif()
  if(MXSD_BINDINGS)
    list(APPEND command_args --bindings)
  endif()
  foreach(path IN LISTS MXSD_STREAM_BIND)
    list(APPEND command_args --stream-bind "${path}")
  endforeach()
  foreach(root IN LISTS MXSD_BIND_ROOT)
    list(APPEND command_args --bind-root "${root}")
  endforeach()

  get_filename_component(mxsdc_directory "${MXSDC_EXECUTABLE}" DIRECTORY)
  file(GLOB mxsdc_modules CONFIGURE_DEPENDS "${mxsdc_directory}/mxsdc_lib/*.py")
  set(dependencies "${schema}" "${MXSDC_EXECUTABLE}" ${mxsdc_modules})
  foreach(dependency IN LISTS MXSD_DEPENDS)
    if(NOT IS_ABSOLUTE "${dependency}")
      get_filename_component(dependency "${dependency}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    list(APPEND dependencies "${dependency}")
  endforeach()

  add_custom_command(
    OUTPUT ${outputs}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${output_dir}"
    COMMAND "${Python3_EXECUTABLE}" "${MXSDC_EXECUTABLE}" ${command_args}
    DEPENDS ${dependencies}
    COMMENT "Generating mxsd validator for ${schema}"
    VERBATIM)

  set_source_files_properties(${outputs} PROPERTIES GENERATED TRUE)
  target_sources("${MXSD_TARGET}" PRIVATE ${generated_sources})
  target_include_directories("${MXSD_TARGET}" PRIVATE "${output_dir}")

  set(${MXSD_TARGET}_MXSD_OUTPUTS "${outputs}" PARENT_SCOPE)
  set(${MXSD_TARGET}_MXSD_INCLUDE_DIR "${output_dir}" PARENT_SCOPE)
endfunction()
