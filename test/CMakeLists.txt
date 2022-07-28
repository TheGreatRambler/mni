cmake_minimum_required(VERSION 3.10)
project(test)

set(CMAKE_CXX_STANDARD 20)

if(CMAKE_BUILD_TYPE EQUAL "DEBUG")
	add_link_options(-g -O0)
endif()

# GoogleTest for testing
set(BUILD_GMOCK OFF)
set(INSTALL_GTEST OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/googletest ${CMAKE_CURRENT_BINARY_DIR}/third_party/googletest)
enable_testing()

add_executable(test ${APPLICATION_TYPE}
	src/encoding.cpp
	src/wasm.cpp
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/wasm-tools/crates/c-api ${CMAKE_CURRENT_BINARY_DIR}/third_party/wasm-tools)

target_include_directories(test PUBLIC include src wasm-tools)
target_compile_options(test PRIVATE -Wall -Wextra -Wno-deprecated-declarations -Wno-unused-variable)

set_target_properties(test PROPERTIES
		OUTPUT_NAME "tinycode_test"
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/..
		CXX_VISIBILITY_PRESET hidden
		POSITION_INDEPENDENT_CODE ON)

target_link_libraries(test PUBLIC tinycode gtest_main wasm-tools)

include(GoogleTest)
gtest_discover_tests(test)