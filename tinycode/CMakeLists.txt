cmake_minimum_required(VERSION 3.10)
project(tinycode)

set(CMAKE_CXX_STANDARD 20)

set(BUILD_EXAMPLES OFF)
set(BUILD_BLACKBOX_TESTS OFF)
set(BUILD_UNIT_TESTS OFF)
set(BUILD_PYTHON_MODULE OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/zxing ${CMAKE_CURRENT_BINARY_DIR}/third_party/zxing)

#set(fpng_SOURCES
#	${CMAKE_CURRENT_SOURCE_DIR}/../third_party/fpng/src/fpng.cpp
#)
#set(fpng_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/fpng/src)

set(tinycode_SOURCES
	src/main.cpp
	src/encoding.cpp
	src/decoding.cpp
	src/tree.cpp
	src/variable.cpp
	src/debug.cpp
)

add_library(tinycode STATIC ${tinycode_SOURCES})
set_target_properties(tinycode PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(tinycode PRIVATE -Wall -Wextra)

target_link_libraries(tinycode PUBLIC ZXing)
target_include_directories(tinycode PUBLIC include ${ZXING_INCLUDE_DIRS})