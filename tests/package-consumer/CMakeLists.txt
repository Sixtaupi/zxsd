cmake_minimum_required(VERSION 3.16)
project(mxsd_package_consumer LANGUAGES C)

include(CTest)
find_package(mxsd CONFIG REQUIRED)

add_executable(mxsd_package_consumer main.c)
mxsd_generate(TARGET mxsd_package_consumer SCHEMA message.xsd ZEPHYR_CONFIG STREAM_BIND message)
target_link_libraries(mxsd_package_consumer PRIVATE mxsd::mxsd)

add_test(NAME mxsd_package_consumer COMMAND mxsd_package_consumer)
