find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(FUZZ_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(GLOB ZXSDC_MODULES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/zxsdc_lib/*.py)
add_custom_command(
  OUTPUT ${FUZZ_GEN}/device.zxsd.c ${FUZZ_GEN}/device.zxsd.h
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/zxsdc
          ${PROJECT_SOURCE_DIR}/tests/device.xsd -o ${FUZZ_GEN}
  DEPENDS ${PROJECT_SOURCE_DIR}/zxsdc ${ZXSDC_MODULES} ${PROJECT_SOURCE_DIR}/tests/device.xsd
  VERBATIM)

add_executable(zxsd_xml_fuzzer fuzz_xml.c ${FUZZ_GEN}/device.zxsd.c)
target_include_directories(zxsd_xml_fuzzer PRIVATE ${FUZZ_GEN})
target_link_libraries(zxsd_xml_fuzzer PRIVATE zxsd)
target_compile_options(zxsd_xml_fuzzer PRIVATE
  -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
target_link_options(zxsd_xml_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
