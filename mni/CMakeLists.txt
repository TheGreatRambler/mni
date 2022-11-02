cmake_minimum_required(VERSION 3.10)
project(mni)

set(CMAKE_CXX_STANDARD 20)

add_compile_options(-Wno-sign-compare)
add_compile_options(-Wno-deprecated-declarations)
add_compile_options(-Wno-unused-parameter)
add_compile_options(-Wno-extern-initializer)

if(CMAKE_BUILD_TYPE EQUAL "DEBUG")
	add_link_options(-g -O0)
endif()

if(ANDROID)
	# Define Android architecture variables
	# Used for building in skia and determining build locations
	if(ANDROID_ABI STREQUAL "armeabi-v7a")
		set(ANDROID_ARCH "arm")
	elseif(ANDROID_ABI STREQUAL "arm64-v8a")
		set(ANDROID_ARCH "arm64")
	elseif(ANDROID_ABI STREQUAL "x86")
		set(ANDROID_ARCH "x86")
	elseif(ANDROID_ABI STREQUAL "x86_64")
		set(ANDROID_ARCH "x64")
	endif()
endif()

# ZXing for QR codes
set(BUILD_EXAMPLES OFF)
set(BUILD_BLACKBOX_TESTS OFF)
set(BUILD_UNIT_TESTS OFF)
set(BUILD_PYTHON_MODULE OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/zxing ${CMAKE_CURRENT_BINARY_DIR}/third_party/zxing)

# Skia for rendering QR codes and programs
set(SKIA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/skia)
set(SKIA_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/third_party/skia)
execute_process(
	COMMAND python tools/git-sync-deps
	WORKING_DIRECTORY ${SKIA_DIR})
if(ANDROID)
	execute_process(
		COMMAND bin/gn gen ${SKIA_BUILD_DIR} "--args=is_debug=false ndk=\"${ANDROID_NDK}\" target_cpu=\"${ANDROID_ARCH}\" is_component_build=true skia_use_system_libjpeg_turbo=false skia_use_system_libwebp=false skia_use_system_expat=false skia_use_system_zlib=false skia_use_system_libpng=false skia_use_system_harfbuzz=false skia_use_system_icu=false skia_use_system_icu=false"
		WORKING_DIRECTORY ${SKIA_DIR})
	add_library(skia SHARED IMPORTED)
	set_property(TARGET skia PROPERTY
		IMPORTED_LOCATION ${SKIA_BUILD_DIR}/libskia.so)
elseif(WIN32)
	execute_process(
		COMMAND bin/gn gen ${SKIA_BUILD_DIR} "--args=is_official_build=true clang_win=\"C:/Program Files/LLVM\" extra_cflags=[\"/MDd\"] skia_use_system_libjpeg_turbo=false skia_use_system_libwebp=false skia_use_system_expat=false skia_use_system_zlib=false skia_use_system_libpng=false skia_use_system_harfbuzz=false skia_use_system_icu=false"
		WORKING_DIRECTORY ${SKIA_DIR})
	add_library(skia STATIC IMPORTED)
	set_property(TARGET skia PROPERTY
		IMPORTED_LOCATION ${SKIA_BUILD_DIR}/skia.lib)
else()
	# MacOS
endif()
execute_process(COMMAND ninja WORKING_DIRECTORY ${SKIA_BUILD_DIR})

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

# wasmtime for an alternative better Webassembly runtime
set(WASMTIME_INCLUDES 
	${CMAKE_CURRENT_SOURCE_DIR}/../third_party/wasmtime/crates/c-api/include
	${CMAKE_CURRENT_SOURCE_DIR}/../third_party/wasmtime/crates/c-api/wasm-c-api/include
)
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/wasmtime/crates/c-api ${CMAKE_CURRENT_BINARY_DIR}/third_party/wasmtime)

# SDL2 for the desktop runtime
if(NOT ANDROID)
	set(SDL_STATIC ON CACHE INTERNAL "" FORCE)
	set(SDL_INCLUDES
		${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SDL/include
		${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SDL/src
	)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SDL ${CMAKE_CURRENT_BINARY_DIR}/third_party/SDL)
endif()

set(mni_SOURCES
	src/encoding.cpp
	src/decoding.cpp
	src/tree.cpp
	src/debug.cpp
	src/export.cpp
	src/import.cpp
	src/wasm.cpp
	src/wasm/parser.cpp
	src/wasm/runtime.cpp
	src/wasm/api.cpp
)

add_library(mni STATIC ${mni_SOURCES})
set_target_properties(mni PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(mni PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-braces)

include_directories(${WASMTIME_INCLUDES})
include_directories(ZXing ${SKIA_DIR} ${SKIA_DIR}/include ${BINARYEN_DIR}/src m3)

if(WIN32)
	target_link_libraries(mni PUBLIC ZXing skia wasmtime binaryen SDL2main SDL2-static m3 gdi32 opengl32)
elseif(ANDROID)
	target_link_libraries(mni PUBLIC ZXing skia wasmtime binaryen m3 GLESv3 EGL)
endif()
target_include_directories(mni PUBLIC include ${SDL_INCLUDES} ${SKIA_DIR} ${SKIA_DIR}/include)