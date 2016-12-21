cmake_minimum_required (VERSION 2.8)
project (v8unpack)

set (v8unpack_VERSION_MAJOR 3)
set (v8unpack_VERSION_MINOR 0)

add_executable (v8unpack src/main.cpp src/V8File.cpp)

set (Boost_USE_STATIC_LIBS ON)
set (Boost_USE_MULTITHREADED ON)
set (Boost_USE_STATIC_RUNTIME OFF)

find_package (Boost 1.53 REQUIRED COMPONENTS filesystem)

include_directories (${Boost_INCLUDE_DIRS})
target_link_libraries (v8unpack ${Boost_LIBRARIES})


find_package (ZLIB REQUIRED)

include_directories (${ZLIB_INCLUDE_DIRS})
target_link_libraries (v8unpack ${ZLIB_LIBRARIES})

set (CPACK_PACKAGE_NAME v8unpack)
set (CPACK_PACKAGE_VERSION 3.0.37)
set (CPACK_DEBIAN_PACKAGE_DEPENDS libboost-filesystem libboost-system zlib1g-dev)
set (CPACK_PACKAGE_CONTACT sergey.batanov@dmpas.ru)
set (CPACK_PACKAGE_DESCRIPTION_SUMMARY Enterprise 8 unpack tool)
set (CPACK_DEBIAN_PACKAGE_SECTION utils)

include (CPack)

