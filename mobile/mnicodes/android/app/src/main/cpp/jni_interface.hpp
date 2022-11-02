#include <jni.h>

// "00024Companion" required for kotlin
#define JNI_METHOD(return_type, method_name)                                                       \
	JNIEXPORT return_type JNICALL Java_com_tgrcode_mnicodes_MniRenderer_00024Companion_##method_name

extern "C" {
JNIEnv* GetJniEnv();

JNI_METHOD(jboolean, loadFromBuffer)(JNIEnv* env, jclass, jbyteArray);
JNI_METHOD(jboolean, renderNextFrame)(JNIEnv* env, jclass);

JNI_METHOD(jboolean, setRotation)(JNIEnv* env, jclass, jint);
}