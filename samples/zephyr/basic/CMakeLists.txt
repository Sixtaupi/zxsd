cmake_minimum_required(VERSION 3.20.0)

set(MXSD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
# Preserve modules discovered by west (including board HALs) while also
# allowing this sample to be built from a standalone mxsd checkout.
list(APPEND EXTRA_ZEPHYR_MODULES ${MXSD_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mxsd_zephyr_test)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(MXSD_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${MXSD_GEN})
execute_process(
  COMMAND ${Python3_EXECUTABLE} ${MXSD_ROOT}/mxsdc ${MXSD_ROOT}/tests/device.xsd -o ${MXSD_GEN}
  COMMAND_ERROR_IS_FATAL ANY)
target_sources(app PRIVATE src/main.c ${MXSD_GEN}/device.mxsd.c)
target_include_directories(app PRIVATE ${MXSD_GEN})
