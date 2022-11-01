#include "jni_interface.hpp"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <android/surface_texture_jni.h>
#include <jni.h>
#include <thread>

#include "native_interface.hpp"

extern "C" {
namespace {
	// maintain a reference to the JVM
	static JavaVM* g_vm = nullptr;
}

jint JNI_OnLoad(JavaVM* vm, void*) {
	g_vm      = vm;
	interface = new NativeInterface();
	return JNI_VERSION_1_6;
}

JNI_METHOD(jboolean, loadFromBuffer)(JNIEnv* env, jclass, jbyteArray buffer) {
	jsize buffer_size     = env->GetArrayLength(buffer);
	uint8_t* buffer_bytes = (uint8_t*)env->GetPrimitiveArrayCritical(buffer, NULL);
	bool success          = interface->LoadFromBuffer(buffer_bytes, buffer_size);
	env->ReleasePrimitiveArrayCritical(buffer, buffer_bytes, 0);
	return (jboolean)success;
}

JNI_METHOD(jboolean, renderNextFrame)(JNIEnv* env, jclass) {
	bool success = interface->RenderNextFrame();
	return (jboolean)success;
}

JNIEnv* GetJniEnv() {
	JNIEnv* env;
	jint result = g_vm->AttachCurrentThread(&env, nullptr);
	return result == JNI_OK ? env : nullptr;
}

jclass FindClass(const char* classname) {
	JNIEnv* env = GetJniEnv();
	return env->FindClass(classname);
}
}