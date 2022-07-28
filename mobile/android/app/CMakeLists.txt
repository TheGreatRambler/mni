cmake_minimum_required(VERSION 3.10)
project(tinycode_android)

#add_library(arcore SHARED IMPORTED)
#set_target_properties(arcore PROPERTIES IMPORTED_LOCATION
#	${ARCORE_LIBPATH}/${ANDROID_ABI}/libarcore_sdk_c.so
#	INTERFACE_INCLUDE_DIRECTORIES ${ARCORE_INCLUDE}
#)

add_subdirectory(${TINYCODE_LOCATION} ${CMAKE_CURRENT_BINARY_DIR}/tinycode)

# The main native app library
add_library(tinycode_android_native SHARED
	src/main/cpp/jni_interface.cpp
	src/main/cpp/native_interface.cpp)

target_include_directories(tinycode_android_native PRIVATE src/main/cpp tinycode)
target_link_libraries(tinycode_android_native android log GLESv2 tinycode)
