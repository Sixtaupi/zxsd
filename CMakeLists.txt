cmake_minimum_required(VERSION 3.16)
project(mxsd VERSION 0.14.0 LANGUAGES C)

include(GNUInstallDirs)

option(MXSD_USE_MXML "Use Mini-XML 4 and build its mxsd streaming adapter" ON)
option(MXSD_BUILD_TESTS "Build the mxsd host test suite" ON)
option(MXSD_NO_HEAP "Disable all runtime heap allocation" OFF)

# Keep Windows DLLs beside test and sample executables so the loader can find them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(MXSD_SOURCES
  src/mxsd.c
  src/mxsd_dom.c
  src/mxsd_identity.c
  src/mxsd_stream.c
  src/mxsd_value.c)
if(MXSD_USE_MXML)
  find_path(MXML_INCLUDE_DIR mxml.h)
  find_library(MXML_LIBRARY NAMES mxml4 mxml)
endif()
if(MXSD_USE_MXML AND MXML_INCLUDE_DIR AND MXML_LIBRARY)
  list(APPEND MXSD_SOURCES src/mxsd_xml_mxml.c)
  set(MXSD_HAS_MXML TRUE)
else()
  list(APPEND MXSD_SOURCES src/mxsd_xml_builtin.c)
  if(MXSD_USE_MXML)
    message(STATUS "Mini-XML 4 not found; using the allocation-free embedded backend")
  endif()
endif()
add_library(mxsd ${MXSD_SOURCES})
add_library(mxsd::mxsd ALIAS mxsd)
target_include_directories(mxsd PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(mxsd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MXSD_HAS_MXML)
  target_include_directories(mxsd PRIVATE ${MXML_INCLUDE_DIR})
  target_link_libraries(mxsd PRIVATE ${MXML_LIBRARY})
  target_compile_definitions(mxsd PRIVATE MXSD_HAS_MXML=1)
endif()
target_compile_features(mxsd PUBLIC c_std_99)
if(MXSD_NO_HEAP)
  target_compile_definitions(mxsd PUBLIC MXSD_NO_HEAP=1)
endif()
set_target_properties(mxsd PROPERTIES
  C_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON)
if(MSVC)
  target_compile_options(mxsd PRIVATE /W4 /permissive-)
else()
  target_compile_options(mxsd PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MXSD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

install(TARGETS mxsd
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/mxsd.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(MXSD_HAS_MXML)
  install(FILES include/mxsd_mxml.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
install(PROGRAMS mxsdc DESTINATION ${CMAKE_INSTALL_BINDIR})
