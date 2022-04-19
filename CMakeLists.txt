cmake_minimum_required(VERSION 3.10)
set(TINYCODE_VERSION "0.0.1")
project(tinycode VERSION ${TINYCODE_VERSION})

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(libtinycode)
add_subdirectory(main)
add_custom_target(tinycode_all ALL DEPENDS main)