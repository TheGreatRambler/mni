#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>

#include "native_interface.hpp"

#define JNI_METHOD(return_type, method_name) JNIEXPORT return_type JNICALL Java_com_tgrcode_teenycodes_JniInterface_##method_name

extern "C" {
namespace {
	// maintain a reference to the JVM
	static JavaVM* g_vm = nullptr;
}

jint JNI_OnLoad(JavaVM* vm, void*) {
	g_vm = vm;
	return JNI_VERSION_1_6;
}

JNI_METHOD(jstring, getAwesomeMessage)(JNIEnv* env, jclass) {
	return env->NewStringUTF((new NativeInterface())->GetMessage().c_str());
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