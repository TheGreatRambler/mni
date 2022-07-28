#include <jni.h>

#define JNI_METHOD_NAME(return_type, method_name) JNIEXPORT return_type JNICALL Java_com_tgrcode_teenycodes_JniInterface_##method_name

extern "C" {
JNIEnv* GetJniEnv();

JNI_METHOD_NAME(jstring, getAwesomeMessage)(JNIEnv* env, jclass);
}