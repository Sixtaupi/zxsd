find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(mxsd_test test_runtime.c)
mxsd_generate(TARGET mxsd_test SCHEMA device.xsd)
target_link_libraries(mxsd_test PRIVATE mxsd)
add_test(NAME runtime COMMAND mxsd_test)
if(MXSD_HAS_MXML)
  add_executable(mxsd_mxml_test test_mxml.c)
  mxsd_generate(TARGET mxsd_mxml_test SCHEMA device.xsd)
  target_include_directories(mxsd_mxml_test PRIVATE ${MXML_INCLUDE_DIR})
  target_link_libraries(mxsd_mxml_test PRIVATE mxsd)
  add_test(NAME mxsd_mxml_test COMMAND mxsd_mxml_test)
endif()
add_test(NAME compiler
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_compiler.py
          ${PROJECT_SOURCE_DIR}/mxsdc
          ${CMAKE_CURRENT_SOURCE_DIR}/device.xsd
          ${CMAKE_CURRENT_SOURCE_DIR}/expected
          ${CMAKE_CURRENT_BINARY_DIR})

add_executable(mxsd_advanced_test test_advanced.c)
mxsd_generate(TARGET mxsd_advanced_test SCHEMA advanced.xsd)
target_link_libraries(mxsd_advanced_test PRIVATE mxsd)
add_test(NAME advanced-runtime COMMAND mxsd_advanced_test)

function(mxsd_add_schema_test name schema source)
  add_executable(${name} ${source})
  mxsd_generate(TARGET ${name} SCHEMA ${schema}.xsd DEPENDS ${ARGN})
  target_link_libraries(${name} PRIVATE mxsd)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

mxsd_add_schema_test(mxsd_derivation_test derivation test_derivation.c)
mxsd_add_schema_test(mxsd_simple_types_test simple_types test_simple_types.c)
mxsd_add_schema_test(mxsd_wildcards_test wildcards test_wildcards.c)
mxsd_add_schema_test(mxsd_identity_test identity test_identity.c)
mxsd_add_schema_test(mxsd_composition_test composition test_composition.c
  ${CMAKE_CURRENT_SOURCE_DIR}/composition_include.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/composition_foreign.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/composition_base.xsd)
mxsd_add_schema_test(mxsd_element_semantics_test element_semantics test_element_semantics.c)
mxsd_add_schema_test(mxsd_groups_test groups test_groups.c)
mxsd_add_schema_test(mxsd_mixed_any_test mixed_any test_mixed_any.c)
mxsd_add_schema_test(mxsd_recursive_test recursive test_recursive.c)
mxsd_add_schema_test(mxsd_regex_test regex test_regex.c)
mxsd_add_schema_test(mxsd_notation_test notation test_notation.c
  ${CMAKE_CURRENT_SOURCE_DIR}/notation_formats.xsd)
mxsd_add_schema_test(mxsd_declarations_test declarations test_declarations.c)

add_executable(mxsd_bindings_test test_bindings.c)
mxsd_generate(TARGET mxsd_bindings_test SCHEMA bindings.xsd BINDINGS)
target_link_libraries(mxsd_bindings_test PRIVATE mxsd)
if(NOT MSVC)
  # Generated bindings must compile cleanly under strict warnings.
  target_compile_options(mxsd_bindings_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME mxsd_bindings_test COMMAND mxsd_bindings_test)

add_test(NAME mxsd_conformance_report
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/conformance/conformance.py
          --manifest ${CMAKE_CURRENT_SOURCE_DIR}/conformance/manifest.json
          --build-dir ${CMAKE_BINARY_DIR}
          --build-config $<CONFIG>
          --ctest ${CMAKE_CTEST_COMMAND}
          --output ${CMAKE_BINARY_DIR}/conformance-report.json)
set(conformance_dependencies
  runtime compiler advanced-runtime mxsd_derivation_test
  mxsd_simple_types_test mxsd_wildcards_test mxsd_identity_test
  mxsd_composition_test mxsd_element_semantics_test mxsd_groups_test
  mxsd_mixed_any_test mxsd_recursive_test mxsd_regex_test
  mxsd_notation_test mxsd_declarations_test mxsd_bindings_test)
set_tests_properties(mxsd_conformance_report PROPERTIES
  DEPENDS "${conformance_dependencies}")

set(MXSD_W3C_SUITE_DIR "" CACHE PATH "Extracted W3C XSD 1.0 test-suite directory")
if(MXSD_W3C_SUITE_DIR)
  add_test(NAME mxsd_w3c_xsd10
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/w3c/run.py
            --suite ${MXSD_W3C_SUITE_DIR}
            --project ${PROJECT_SOURCE_DIR}
            --work-dir ${CMAKE_CURRENT_BINARY_DIR}/w3c
            --output ${CMAKE_BINARY_DIR}/w3c-xsd10-report.json
            --html ${CMAKE_BINARY_DIR}/w3c-xsd10-report.html
            --cc ${CMAKE_C_COMPILER})
  set_tests_properties(mxsd_w3c_xsd10 PROPERTIES LABELS "conformance;w3c" TIMEOUT 7200)
endif()
