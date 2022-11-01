cmake_minimum_required(VERSION 3.10)
project(mni_android)

#add_library(arcore SHARED IMPORTED)
#set_target_properties(arcore PROPERTIES IMPORTED_LOCATION
#	${ARCORE_LIBPATH}/${ANDROID_ABI}/libarcore_sdk_c.so
#	INTERFACE_INCLUDE_DIRECTORIES ${ARCORE_INCLUDE}
#)

add_subdirectory(${MNI_LOCATION} ${CMAKE_CURRENT_BINARY_DIR}/mni)

# The main native app library
add_library(mni_android_native SHARED
	src/main/cpp/jni_interface.cpp
	src/main/cpp/native_interface.cpp)

target_include_directories(mni_android_native PRIVATE src/main/cpp mni)
target_link_libraries(mni_android_native log android mni GLESv3 EGL)
