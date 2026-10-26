cmake_minimum_required(VERSION 3.16)
project(mxsd_package_consumer LANGUAGES C)

include(CTest)
find_package(mxsd CONFIG REQUIRED)

add_executable(mxsd_package_consumer main.c)
mxsd_generate(TARGET mxsd_package_consumer SCHEMA message.xsd ZEPHYR_CONFIG STREAM_BIND message)
target_link_libraries(mxsd_package_consumer PRIVATE mxsd::mxsd)

# Linking the installed import library does not put its DLL on the loader's
# search path. Stage the selected configuration's runtime beside the executable.
get_target_property(mxsd_library_type mxsd::mxsd TYPE)
if(WIN32 AND mxsd_library_type STREQUAL "SHARED_LIBRARY")
  add_custom_command(TARGET mxsd_package_consumer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:mxsd::mxsd>" "$<TARGET_FILE_DIR:mxsd_package_consumer>"
    VERBATIM)
endif()

add_test(NAME mxsd_package_consumer COMMAND mxsd_package_consumer)
set_tests_properties(mxsd_package_consumer PROPERTIES TIMEOUT 30)
