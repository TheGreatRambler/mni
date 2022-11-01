cmake_minimum_required(VERSION 3.10)
set(MNI_VERSION "0.0.1")
project(mni VERSION ${MNI_VERSION})

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(mni)
add_subdirectory(main)
add_subdirectory(test)
add_custom_target(mni_all ALL DEPENDS main test)