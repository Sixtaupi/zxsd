find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(FUZZ_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(
  OUTPUT ${FUZZ_GEN}/device.mxsd.c ${FUZZ_GEN}/device.mxsd.h
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/mxsdc
          ${PROJECT_SOURCE_DIR}/tests/device.xsd -o ${FUZZ_GEN}
  DEPENDS ${PROJECT_SOURCE_DIR}/mxsdc ${PROJECT_SOURCE_DIR}/tests/device.xsd
  VERBATIM)

add_executable(mxsd_xml_fuzzer fuzz_xml.c ${FUZZ_GEN}/device.mxsd.c)
target_include_directories(mxsd_xml_fuzzer PRIVATE ${FUZZ_GEN})
target_link_libraries(mxsd_xml_fuzzer PRIVATE mxsd)
target_compile_options(mxsd_xml_fuzzer PRIVATE
  -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
target_link_options(mxsd_xml_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
