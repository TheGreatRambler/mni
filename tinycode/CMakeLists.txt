cmake_minimum_required(VERSION 3.10)
project(tinycode)

set(CMAKE_CXX_STANDARD 20)

add_compile_options(-Wno-ambiguous-reversed-operator)
add_compile_options(-Wno-deprecated-declarations)
add_compile_options(-Wno-unused-parameter)
add_compile_options(-Wno-extern-initializer)

# ZXing for QR codes
set(BUILD_EXAMPLES OFF)
set(BUILD_BLACKBOX_TESTS OFF)
set(BUILD_UNIT_TESTS OFF)
set(BUILD_PYTHON_MODULE OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/zxing ${CMAKE_CURRENT_BINARY_DIR}/third_party/zxing)

# Skia for rendering QR codes and programs
set(SKIA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/skia)
set(SKIA_BUILD_TYPE out/Static)
set(SKIA_CMAKE_DIR ${SKIA_DIR}/${SKIA_BUILD_TYPE})
set(SKIA_LINK ${SKIA_CMAKE_DIR})
execute_process(
	COMMAND python tools/git-sync-deps
	WORKING_DIRECTORY ${SKIA_DIR})
execute_process(
	COMMAND bin/gn gen ${SKIA_BUILD_TYPE} "--args=is_official_build=true clang_win=\"C:/Program Files/LLVM\" extra_cflags=[\"/MDd\"] skia_use_system_libjpeg_turbo=false skia_use_system_libwebp=false skia_use_expat=false skia_use_system_zlib=false skia_use_system_libpng=false skia_use_system_harfbuzz=false skia_use_system_icu=false"
	WORKING_DIRECTORY ${SKIA_DIR})
execute_process(COMMAND ninja WORKING_DIRECTORY ${SKIA_CMAKE_DIR})

# Binaryen for webassembly
set(BINARYEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/binaryen)
set(CMAKE_CXX_STANDARD 17)
set(BUILD_TESTS OFF CACHE INTERNAL "" FORCE)
set(BUILD_STATIC_LIB ON CACHE INTERNAL "" FORCE)
set(BUILD_TOOLS OFF CACHE INTERNAL "" FORCE)
add_subdirectory(${BINARYEN_DIR} ${CMAKE_CURRENT_BINARY_DIR}/third_party/binaryen)
set(CMAKE_CXX_STANDARD 20)

# wasm3 for the Webassembly runtime
set(wasm3_includes
	${CMAKE_CURRENT_SOURCE_DIR}/../third_party/wasm3/source
	${CMAKE_CURRENT_SOURCE_DIR}/../third_party/wasm3/platforms/cpp/wasm3_cpp/include
)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/wasm3/source ${CMAKE_CURRENT_BINARY_DIR}/third_party/wasm3)

set(tinycode_SOURCES
	src/main.cpp
	src/encoding.cpp
	src/decoding.cpp
	src/tree.cpp
	src/variable.cpp
	src/debug.cpp
	src/export.cpp
	src/import.cpp
	src/wasm.cpp
)

add_library(tinycode STATIC ${tinycode_SOURCES})
set_target_properties(tinycode PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(tinycode PRIVATE -Wall -Wextra)

include_directories(${ZXING_INCLUDE_DIRS} ${SKIA_DIR} ${SKIA_DIR}/include/core ${SKIA_DIR}/include/encode ${BINARYEN_DIR}/src ${wasm3_includes})

target_link_libraries(tinycode PUBLIC ZXing ${SKIA_LINK}/skia.lib binaryen m3 gdi32 opengl32)
target_include_directories(tinycode PUBLIC include)