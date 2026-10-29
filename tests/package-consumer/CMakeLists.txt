cmake_minimum_required(VERSION 3.16)
project(zxsd_package_consumer LANGUAGES C)

include(CTest)
find_package(zxsd CONFIG REQUIRED)

add_executable(zxsd_package_consumer main.c)
zxsd_generate(TARGET zxsd_package_consumer SCHEMA message.xsd ZEPHYR_CONFIG STREAM_BIND message)
target_link_libraries(zxsd_package_consumer PRIVATE zxsd::zxsd)

# Linking the installed import library does not put its DLL on the loader's
# search path. Stage the selected configuration's runtime beside the executable.
get_target_property(zxsd_library_type zxsd::zxsd TYPE)
if(WIN32 AND zxsd_library_type STREQUAL "SHARED_LIBRARY")
  add_custom_command(TARGET zxsd_package_consumer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:zxsd::zxsd>" "$<TARGET_FILE_DIR:zxsd_package_consumer>"
    VERBATIM)
endif()

add_test(NAME zxsd_package_consumer COMMAND zxsd_package_consumer)
set_tests_properties(zxsd_package_consumer PROPERTIES TIMEOUT 30)
