cmake_minimum_required(VERSION 3.20.0)

set(MXSD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
# The standalone sample is outside a west workspace, so provide the module
# list explicitly. Applications in a west workspace discover mxsd normally.
list(APPEND ZEPHYR_MODULES ${MXSD_ROOT})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mxsd_zephyr_bindings)

# Generate the validator and the typed bindings on the host. Production
# applications may run this as a build step or commit the generated files.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(MXSD_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${MXSD_GEN})
execute_process(
  COMMAND ${Python3_EXECUTABLE} ${MXSD_ROOT}/mxsdc
          ${CMAKE_CURRENT_SOURCE_DIR}/settings.xsd -o ${MXSD_GEN} --bindings
  COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/settings.xsd ${MXSD_ROOT}/mxsdc)
target_sources(app PRIVATE
  src/main.c
  ${MXSD_GEN}/settings.mxsd.c
  ${MXSD_GEN}/settings.bind.c)
target_include_directories(app PRIVATE ${MXSD_GEN})
