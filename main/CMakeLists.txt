cmake_minimum_required(VERSION 3.10)
project(main)

set(CMAKE_CXX_STANDARD 20)

add_executable(main ${APPLICATION_TYPE}
	src/main.cpp
	src/fmt/format.cpp
	src/fmt/os.cpp
)

target_include_directories(main PUBLIC include src)
#target_compile_options(main PRIVATE -Wall -Wextra -Werror)
target_compile_options(main PRIVATE -Wall -Wextra)

set_target_properties(main PROPERTIES
		OUTPUT_NAME "tinycode"
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/..
		CXX_VISIBILITY_PRESET hidden
		POSITION_INDEPENDENT_CODE ON)

target_link_libraries(main PUBLIC tinycode pthread)