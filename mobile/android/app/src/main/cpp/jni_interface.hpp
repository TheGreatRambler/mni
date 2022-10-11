#include <jni.h>

#define JNI_METHOD(return_type, method_name)                                                       \
	JNIEXPORT return_type JNICALL Java_com_tgrcode_teenycodes_MainActivity_##method_name

extern "C" {
JNIEnv* GetJniEnv();

JNI_METHOD(jboolean, registerCode)(JNIEnv* env, jclass, jbyteArray qr);
JNI_METHOD(jstring, getCodeName)(JNIEnv* env, jclass);
}