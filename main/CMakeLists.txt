cmake_minimum_required(VERSION 3.10)
project(main)

set(CMAKE_CXX_STANDARD 20)


set(BUILD_EXAMPLES OFF)
set(BUILD_BLACKBOX_TESTS OFF)
set(BUILD_UNIT_TESTS OFF)
set(BUILD_PYTHON_MODULE OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/zxing ${CMAKE_CURRENT_BINARY_DIR}/third_party/zxing)

add_executable(main ${APPLICATION_TYPE}
	src/main.cpp
	src/fmt/format.cpp
	src/fmt/os.cpp
)

target_include_directories(main PUBLIC include src ${ZXING_INCLUDE_DIRS})
#target_compile_options(main PRIVATE -Wall -Wextra -Werror)
target_compile_options(main PRIVATE -Wall -Wextra)

set_target_properties(main PROPERTIES
		OUTPUT_NAME "tinycode"
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/..
		CXX_VISIBILITY_PRESET hidden
		POSITION_INDEPENDENT_CODE ON)

target_link_libraries(main PUBLIC tinycode pthread ZXing)