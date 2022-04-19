cmake_minimum_required(VERSION 3.10)
project(libtinycode)

set(CMAKE_CXX_STANDARD 20)

#option(BUILD_EXAMPLES OFF)
#option(BUILD_PYTHON_MODULE OFF)
#option(BUILD_CACHED_CUDA_MANAGER OFF)
#add_subdirectory(third_party/Open3D)

set(LIBTINYCODE_SOURCES
	src/main.cpp
	src/encoding.cpp
	src/decoding.cpp
	src/tree.cpp
	src/variable.cpp
	src/debug.cpp
)

add_library(libtinycode STATIC ${LIBTINYCODE_SOURCES})
set_target_properties(libtinycode PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(libtinycode PRIVATE -Wall -Wextra)

#target_include_directories(libmodeler PUBLIC include Open3D)
#target_link_libraries(libmodeler PUBLIC Open3D)
target_include_directories(libtinycode PUBLIC include)