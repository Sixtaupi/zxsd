find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME compatibility-helpers
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_compatibility_helpers.py)
add_test(NAME compiler-modules
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_compiler_modules.py
          ${PROJECT_SOURCE_DIR})
add_executable(zxsd_compatibility_test test_compatibility.c)
target_link_libraries(zxsd_compatibility_test PRIVATE zxsd)
add_test(NAME zxsd_compatibility_test COMMAND zxsd_compatibility_test)
add_test(NAME generated-compatibility
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_compatibility.py
          --project ${PROJECT_SOURCE_DIR} --build ${CMAKE_BINARY_DIR}
          --cc ${CMAKE_C_COMPILER} --compiler-id ${CMAKE_C_COMPILER_ID}
          --cmake ${CMAKE_COMMAND})
add_executable(zxsd_test test_runtime.c)
zxsd_generate(TARGET zxsd_test SCHEMA device.xsd)
target_link_libraries(zxsd_test PRIVATE zxsd)
add_test(NAME runtime COMMAND zxsd_test)
if(ZXSD_HAS_MXML)
  add_executable(zxsd_mxml_test test_mxml.c)
  zxsd_generate(TARGET zxsd_mxml_test SCHEMA device.xsd)
  target_include_directories(zxsd_mxml_test PRIVATE ${MXML_INCLUDE_DIR})
  target_link_libraries(zxsd_mxml_test PRIVATE zxsd)
  add_test(NAME zxsd_mxml_test COMMAND zxsd_mxml_test)
endif()
add_test(NAME compiler
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_compiler.py
          ${PROJECT_SOURCE_DIR}/zxsdc
          ${CMAKE_CURRENT_SOURCE_DIR}/device.xsd
          ${CMAKE_CURRENT_SOURCE_DIR}/expected
          ${CMAKE_CURRENT_BINARY_DIR})

add_executable(zxsd_advanced_test test_advanced.c)
zxsd_generate(TARGET zxsd_advanced_test SCHEMA advanced.xsd)
target_link_libraries(zxsd_advanced_test PRIVATE zxsd)
add_test(NAME advanced-runtime COMMAND zxsd_advanced_test)

function(zxsd_add_schema_test name schema source)
  add_executable(${name} ${source})
  zxsd_generate(TARGET ${name} SCHEMA ${schema}.xsd DEPENDS ${ARGN})
  target_link_libraries(${name} PRIVATE zxsd)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

zxsd_add_schema_test(zxsd_derivation_test derivation test_derivation.c)
zxsd_add_schema_test(zxsd_simple_types_test simple_types test_simple_types.c)
zxsd_add_schema_test(zxsd_wildcards_test wildcards test_wildcards.c)
zxsd_add_schema_test(zxsd_identity_test identity test_identity.c)
zxsd_add_schema_test(zxsd_composition_test composition test_composition.c
  ${CMAKE_CURRENT_SOURCE_DIR}/composition_include.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/composition_foreign.xsd
  ${CMAKE_CURRENT_SOURCE_DIR}/composition_base.xsd)
zxsd_add_schema_test(zxsd_element_semantics_test element_semantics test_element_semantics.c)
zxsd_add_schema_test(zxsd_groups_test groups test_groups.c)
zxsd_add_schema_test(zxsd_stream_models_test stream_models test_stream_models.c)
# Keep this pool-boundary executable independent of the normal runtime ABI.
add_executable(zxsd_model_limits_test test_model_limits.c ${PROJECT_SOURCE_DIR}/src/zxsd_model.c)
target_include_directories(zxsd_model_limits_test PRIVATE
  ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)
# Force the private test configuration into both translation units. Visual Studio
# may reorder /D and /U flags into separate MSBuild properties, so command-line
# undefine/redefine pairs cannot reliably override the public default there.
if(MSVC)
  target_compile_options(zxsd_model_limits_test PRIVATE
    "/FI${CMAKE_CURRENT_SOURCE_DIR}/model_limits_config.h")
else()
  target_compile_options(zxsd_model_limits_test PRIVATE
    -include "${CMAKE_CURRENT_SOURCE_DIR}/model_limits_config.h")
endif()
target_compile_features(zxsd_model_limits_test PRIVATE c_std_99)
set_target_properties(zxsd_model_limits_test PROPERTIES C_EXTENSIONS OFF)
add_test(NAME zxsd_model_limits_test COMMAND zxsd_model_limits_test)
zxsd_add_schema_test(zxsd_mixed_any_test mixed_any test_mixed_any.c)
zxsd_add_schema_test(zxsd_recursive_test recursive test_recursive.c)
zxsd_add_schema_test(zxsd_regex_test regex test_regex.c)
zxsd_add_schema_test(zxsd_notation_test notation test_notation.c
  ${CMAKE_CURRENT_SOURCE_DIR}/notation_formats.xsd)
zxsd_add_schema_test(zxsd_declarations_test declarations test_declarations.c)

add_executable(zxsd_bindings_test test_bindings.c)
zxsd_generate(TARGET zxsd_bindings_test SCHEMA bindings.xsd BINDINGS)
target_link_libraries(zxsd_bindings_test PRIVATE zxsd)
if(NOT MSVC)
  # Generated bindings must compile cleanly under strict warnings.
  target_compile_options(zxsd_bindings_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME zxsd_bindings_test COMMAND zxsd_bindings_test)

add_executable(zxsd_stream_bindings_test test_stream_bindings.c)
zxsd_generate(TARGET zxsd_stream_bindings_test SCHEMA stream_bindings.xsd
  BINDINGS BIND_ROOT row STREAM_BIND doc/row doc/row/label row uniqueDoc/row)
target_link_libraries(zxsd_stream_bindings_test PRIVATE zxsd)
add_test(NAME zxsd_stream_bindings_test COMMAND zxsd_stream_bindings_test)

set(weather_source ${PROJECT_SOURCE_DIR}/samples/zephyr/weather)
add_executable(zxsd_weather_binding_test test_weather_binding.c
  ${weather_source}/src/weather_summary.c)
target_include_directories(zxsd_weather_binding_test PRIVATE ${weather_source}/src)
zxsd_generate(TARGET zxsd_weather_binding_test SCHEMA ${weather_source}/met_weather.xsd
  STREAM_BIND weatherdata/product/time)
target_link_libraries(zxsd_weather_binding_test PRIVATE zxsd)
add_test(NAME zxsd_weather_binding_test COMMAND zxsd_weather_binding_test)

add_test(NAME zxsd_conformance_report
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/conformance/conformance.py
          --manifest ${CMAKE_CURRENT_SOURCE_DIR}/conformance/manifest.json
          --build-dir ${CMAKE_BINARY_DIR}
          --build-config $<CONFIG>
          --ctest ${CMAKE_CTEST_COMMAND}
          --output ${CMAKE_BINARY_DIR}/conformance-report.json)
set(conformance_dependencies
  runtime compiler generated-compatibility zxsd_compatibility_test advanced-runtime zxsd_derivation_test
  zxsd_simple_types_test zxsd_wildcards_test zxsd_identity_test
  zxsd_composition_test zxsd_element_semantics_test zxsd_groups_test
  zxsd_mixed_any_test zxsd_recursive_test zxsd_regex_test
  zxsd_notation_test zxsd_declarations_test zxsd_bindings_test zxsd_stream_models_test
  zxsd_model_limits_test zxsd_stream_bindings_test zxsd_weather_binding_test)
set_tests_properties(zxsd_conformance_report PROPERTIES
  DEPENDS "${conformance_dependencies}")

set(ZXSD_W3C_SUITE_DIR "" CACHE PATH "Extracted W3C XSD 1.0 test-suite directory")
if(ZXSD_W3C_SUITE_DIR)
  add_test(NAME zxsd_w3c_xsd10
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/w3c/run.py
            --suite ${ZXSD_W3C_SUITE_DIR}
            --project ${PROJECT_SOURCE_DIR}
            --work-dir ${CMAKE_CURRENT_BINARY_DIR}/w3c
            --output ${CMAKE_BINARY_DIR}/w3c-xsd10-report.json
            --html ${CMAKE_BINARY_DIR}/w3c-xsd10-report.html
            --cc ${CMAKE_C_COMPILER})
  set_tests_properties(zxsd_w3c_xsd10 PROPERTIES LABELS "conformance;w3c" TIMEOUT 7200)
endif()
