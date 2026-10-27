cmake_minimum_required(VERSION 3.16)
project(mxsd VERSION 0.20.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(cmake/mxsdGenerate.cmake)

# Before 1.0, a new minor release may break the public API/ABI.
if(PROJECT_VERSION_MAJOR EQUAL 0)
  set(MXSD_SOVERSION "0.${PROJECT_VERSION_MINOR}")
  set(MXSD_PACKAGE_COMPATIBILITY SameMinorVersion)
else()
  set(MXSD_SOVERSION "${PROJECT_VERSION_MAJOR}")
  set(MXSD_PACKAGE_COMPATIBILITY SameMajorVersion)
endif()
file(STRINGS include/mxsd_version.h mxsd_header_version REGEX "^#define MXSD_VERSION[ \t]+")
string(REGEX REPLACE "^#define MXSD_VERSION[ \t]+\"([^\"]+)\".*" "\\1"
  mxsd_header_version "${mxsd_header_version}")
if(NOT mxsd_header_version STREQUAL PROJECT_VERSION)
  message(FATAL_ERROR "Synchronize MXSD_VERSION in include/mxsd_version.h with PROJECT_VERSION")
endif()

option(MXSD_USE_MXML "Use Mini-XML 4 and build its mxsd streaming adapter" ON)
option(MXSD_REQUIRE_MXML "Fail configuration when Mini-XML 4 is unavailable" OFF)
option(MXSD_BUILD_TESTS "Build the mxsd host test suite" ON)
option(MXSD_BUILD_FUZZERS "Build libFuzzer targets (Clang only)" OFF)
option(MXSD_NO_HEAP "Disable all runtime heap allocation" OFF)

# Keep Windows DLLs beside test and sample executables so the loader can find them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(MXSD_SOURCES
  src/mxsd.c
  src/mxsd_bind.c
  src/mxsd_bind_stream.c
  src/mxsd_dom.c
  src/mxsd_identity.c
  src/mxsd_model.c
  src/mxsd_regex.c
  src/mxsd_stream.c
  src/mxsd_xml_stream.c
  src/mxsd_unicode.c
  src/mxsd_value.c)
list(APPEND MXSD_SOURCES src/mxsd_version.c)
if(MXSD_USE_MXML)
  find_path(MXML_INCLUDE_DIR mxml.h)
  find_library(MXML_LIBRARY NAMES mxml4 mxml)
endif()
if(MXSD_USE_MXML AND MXML_INCLUDE_DIR AND MXML_LIBRARY)
  list(APPEND MXSD_SOURCES src/mxsd_xml_mxml.c)
  set(MXSD_HAS_MXML TRUE)
else()
  if(MXSD_REQUIRE_MXML)
    message(FATAL_ERROR "MXSD_REQUIRE_MXML=ON but Mini-XML 4 was not found")
  endif()
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
  VERSION ${PROJECT_VERSION}
  SOVERSION ${MXSD_SOVERSION}
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

if(MXSD_BUILD_FUZZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "MXSD_BUILD_FUZZERS requires Clang and libFuzzer")
  endif()
  target_compile_options(mxsd PRIVATE
    -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
  add_subdirectory(fuzz)
endif()

set(MXSD_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/mxsd)
configure_package_config_file(
  cmake/mxsdConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/mxsdConfig.cmake
  INSTALL_DESTINATION ${MXSD_INSTALL_CMAKEDIR}
  PATH_VARS CMAKE_INSTALL_BINDIR)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/mxsdConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY ${MXSD_PACKAGE_COMPATIBILITY})
configure_file(cmake/mxsd.pc.in ${CMAKE_CURRENT_BINARY_DIR}/mxsd.pc @ONLY)

install(TARGETS mxsd
  EXPORT mxsdTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/mxsd.h include/mxsd_bind.h include/mxsd_version.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(MXSD_HAS_MXML)
  install(FILES include/mxsd_mxml.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
install(PROGRAMS mxsdc DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY mxsdc_lib DESTINATION ${CMAKE_INSTALL_BINDIR}
  FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
install(EXPORT mxsdTargets
  FILE mxsdTargets.cmake
  NAMESPACE mxsd::
  DESTINATION ${MXSD_INSTALL_CMAKEDIR})
install(FILES
  cmake/mxsdGenerate.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/mxsdConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/mxsdConfigVersion.cmake
  DESTINATION ${MXSD_INSTALL_CMAKEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mxsd.pc
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
