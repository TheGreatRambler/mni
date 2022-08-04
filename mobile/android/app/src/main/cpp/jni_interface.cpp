#include "jni_interface.hpp"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>

#include "native_interface.hpp"

extern "C" {
namespace {
	// maintain a reference to the JVM
	static JavaVM* g_vm = nullptr;
	// Native interface class
	static NativeInterface* interface = nullptr;
}

jint JNI_OnLoad(JavaVM* vm, void*) {
	g_vm      = vm;
	interface = new NativeInterface();
	return JNI_VERSION_1_6;
}

JNI_METHOD(void, registerCode)(JNIEnv* env, jclass, jbyteArray qr) {
	jsize qr_size     = env->GetArrayLength(qr);
	uint8_t* qr_bytes = (uint8_t*)env->GetPrimitiveArrayCritical(qr, NULL);
	interface->LoadCode(qr_bytes, qr_size);
	env->ReleasePrimitiveArrayCritical(qr, qr_bytes, 0);
}

JNI_METHOD(jstring, getCodeName)(JNIEnv* env, jclass) {
	return env->NewStringUTF(interface->GetCodeName().c_str());
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