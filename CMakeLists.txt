cmake_minimum_required(VERSION 3.16)
project(zxsd VERSION 0.20.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(cmake/zxsdGenerate.cmake)

# Before 1.0, a new minor release may break the public API/ABI.
if(PROJECT_VERSION_MAJOR EQUAL 0)
  set(ZXSD_SOVERSION "0.${PROJECT_VERSION_MINOR}")
  set(ZXSD_PACKAGE_COMPATIBILITY SameMinorVersion)
else()
  set(ZXSD_SOVERSION "${PROJECT_VERSION_MAJOR}")
  set(ZXSD_PACKAGE_COMPATIBILITY SameMajorVersion)
endif()
file(STRINGS include/zxsd_version.h zxsd_header_version REGEX "^#define ZXSD_VERSION[ \t]+")
string(REGEX REPLACE "^#define ZXSD_VERSION[ \t]+\"([^\"]+)\".*" "\\1"
  zxsd_header_version "${zxsd_header_version}")
if(NOT zxsd_header_version STREQUAL PROJECT_VERSION)
  message(FATAL_ERROR "Synchronize ZXSD_VERSION in include/zxsd_version.h with PROJECT_VERSION")
endif()

option(ZXSD_USE_MXML "Use Mini-XML 4 and build its zxsd streaming adapter" ON)
option(ZXSD_REQUIRE_MXML "Fail configuration when Mini-XML 4 is unavailable" OFF)
option(ZXSD_BUILD_TESTS "Build the zxsd host test suite" ON)
option(ZXSD_BUILD_FUZZERS "Build libFuzzer targets (Clang only)" OFF)
option(ZXSD_NO_HEAP "Disable all runtime heap allocation" OFF)

# Keep Windows DLLs beside test and sample executables so the loader can find them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(ZXSD_SOURCES
  src/zxsd.c
  src/zxsd_bind.c
  src/zxsd_bind_stream.c
  src/zxsd_dom.c
  src/zxsd_identity.c
  src/zxsd_model.c
  src/zxsd_regex.c
  src/zxsd_stream.c
  src/zxsd_xml_stream.c
  src/zxsd_unicode.c
  src/zxsd_value.c)
list(APPEND ZXSD_SOURCES src/zxsd_version.c)
if(ZXSD_USE_MXML)
  find_path(MXML_INCLUDE_DIR mxml.h)
  find_library(MXML_LIBRARY NAMES mxml4 mxml)
endif()
if(ZXSD_USE_MXML AND MXML_INCLUDE_DIR AND MXML_LIBRARY)
  list(APPEND ZXSD_SOURCES src/zxsd_xml_mxml.c)
  set(ZXSD_HAS_MXML TRUE)
else()
  if(ZXSD_REQUIRE_MXML)
    message(FATAL_ERROR "ZXSD_REQUIRE_MXML=ON but Mini-XML 4 was not found")
  endif()
  list(APPEND ZXSD_SOURCES src/zxsd_xml_builtin.c)
  if(ZXSD_USE_MXML)
    message(STATUS "Mini-XML 4 not found; using the allocation-free embedded backend")
  endif()
endif()
add_library(zxsd ${ZXSD_SOURCES})
add_library(zxsd::zxsd ALIAS zxsd)
target_include_directories(zxsd PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(zxsd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(ZXSD_HAS_MXML)
  target_include_directories(zxsd PRIVATE ${MXML_INCLUDE_DIR})
  target_link_libraries(zxsd PRIVATE ${MXML_LIBRARY})
  target_compile_definitions(zxsd PRIVATE ZXSD_HAS_MXML=1)
endif()
target_compile_features(zxsd PUBLIC c_std_99)
if(ZXSD_NO_HEAP)
  target_compile_definitions(zxsd PUBLIC ZXSD_NO_HEAP=1)
endif()
set_target_properties(zxsd PROPERTIES
  C_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  VERSION ${PROJECT_VERSION}
  SOVERSION ${ZXSD_SOVERSION}
  WINDOWS_EXPORT_ALL_SYMBOLS ON)
if(MSVC)
  target_compile_options(zxsd PRIVATE /W4 /permissive-)
else()
  target_compile_options(zxsd PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ZXSD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(ZXSD_BUILD_FUZZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "ZXSD_BUILD_FUZZERS requires Clang and libFuzzer")
  endif()
  target_compile_options(zxsd PRIVATE
    -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
  add_subdirectory(fuzz)
endif()

set(ZXSD_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/zxsd)
configure_package_config_file(
  cmake/zxsdConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/zxsdConfig.cmake
  INSTALL_DESTINATION ${ZXSD_INSTALL_CMAKEDIR}
  PATH_VARS CMAKE_INSTALL_BINDIR)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/zxsdConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY ${ZXSD_PACKAGE_COMPATIBILITY})
configure_file(cmake/zxsd.pc.in ${CMAKE_CURRENT_BINARY_DIR}/zxsd.pc @ONLY)

install(TARGETS zxsd
  EXPORT zxsdTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/zxsd.h include/zxsd_bind.h include/zxsd_version.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(ZXSD_HAS_MXML)
  install(FILES include/zxsd_mxml.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
install(PROGRAMS zxsdc DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY zxsdc_lib DESTINATION ${CMAKE_INSTALL_BINDIR}
  FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
install(EXPORT zxsdTargets
  FILE zxsdTargets.cmake
  NAMESPACE zxsd::
  DESTINATION ${ZXSD_INSTALL_CMAKEDIR})
install(FILES
  cmake/zxsdGenerate.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/zxsdConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/zxsdConfigVersion.cmake
  DESTINATION ${ZXSD_INSTALL_CMAKEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/zxsd.pc
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
