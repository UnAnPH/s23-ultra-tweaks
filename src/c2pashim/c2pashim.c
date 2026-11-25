// SPDX-License-Identifier: GPL-2.0
/*
 * c2pashim - lets the S23's Visual Cloud Core use the S23's own C2PA library under galaxyai (issue #19).
 *
 * Visual Cloud Core (com.samsung.android.visual.cloudcore) picks its C2PA adapter from
 * SEC_FLOATING_FEATURE_COMMON_CONFIG_AI_VERSION: >= 20261 -> "V2", else "V1". galaxyai raises that
 * key to 20263 (the S24 Smart Suggestions gates Now Brief on it), and floating_feature.xml is parsed
 * in zygote (WallpaperManager's static init), so every app sees it. The V2 adapter then calls four
 * natives the S23's libC2paDps.camera.samsung.so (V1) doesn't have, and Visual Cloud Core crashes with
 * UnsatisfiedLinkError C2paLib.getVersion().
 *
 * This library is bound over /system/lib64/libC2paDps.camera.samsung.so. The original is added next to
 * it as libC2paDps_s23.camera.samsung.so. All twelve JNI methods of
 * com.samsung.android.visual.cloudcore.dps.service.binder.C2paLib are exported here: the eight the S23
 * library has are forwarded unchanged; the four V2-only ones are mapped onto V1 the way the V1 adapter
 * does it:
 *   getVersion()                 -> "1.0.0" (the V1 adapter's constant; only logged)
 *   enableTrustCheck(a, s, c)    -> remember the three trust-file paths, "Success"
 *   readFromFd(fd, mime)         -> readFromFdWithTrustCheck(a, s, c, fd, mime)
 *   readFromFile(path)           -> readFromFileWithTrustCheck(a, s, c, path)
 * Until enableTrustCheck runs, the paths default to the ones V1 uses (the app's external
 * files/Download/{anchors.pem,allowed.sha256.txt,store.cfg} for user 0).
 */
#include <dlfcn.h>
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define TAG "s23tweaks-c2pashim"
#define ORIG "libC2paDps_s23.camera.samsung.so"
#define P(n) Java_com_samsung_android_visual_cloudcore_dps_service_binder_C2paLib_##n
#define DEF_DIR "/storage/emulated/0/Android/data/com.samsung.android.visual.cloudcore/files/Download/"

static void *h;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static char *trust[3];

static void load(void)
{
	h = dlopen(ORIG, RTLD_NOW);
	if (!h)
		__android_log_print(ANDROID_LOG_ERROR, TAG, "dlopen %s: %s", ORIG, dlerror());
}

static void *sym(JNIEnv *env, const char *name)
{
	pthread_once(&once, load);
	void *f = h ? dlsym(h, name) : NULL;
	if (!f) {
		jclass c = (*env)->FindClass(env, "java/lang/UnsatisfiedLinkError");
		if (c)
			(*env)->ThrowNew(env, c, name);
	}
	return f;
}
#define ORIGFN(type, name, ...) \
	type (*o)(JNIEnv *, jobject, __VA_ARGS__) = sym(env, "Java_com_samsung_android_visual_cloudcore_dps_service_binder_C2paLib_" #name); \
	if (!o) return NULL

/* the trust paths as Java strings: what enableTrustCheck stored, else V1's defaults */
static void trust_strings(JNIEnv *env, jstring out[3])
{
	static const char *def[3] = { DEF_DIR "anchors.pem", DEF_DIR "allowed.sha256.txt", DEF_DIR "store.cfg" };
	pthread_mutex_lock(&mu);
	for (int i = 0; i < 3; i++)
		out[i] = (*env)->NewStringUTF(env, trust[i] ? trust[i] : def[i]);
	pthread_mutex_unlock(&mu);
}

/* ---- V2-only methods, mapped onto V1 ---- */
JNIEXPORT jstring JNICALL P(getVersion)(JNIEnv *env, jobject thiz)
{
	(void)thiz;
	return (*env)->NewStringUTF(env, "1.0.0");
}

JNIEXPORT jstring JNICALL P(enableTrustCheck)(JNIEnv *env, jobject thiz, jstring a, jstring b, jstring c)
{
	(void)thiz;
	jstring in[3] = { a, b, c };
	pthread_mutex_lock(&mu);
	for (int i = 0; i < 3; i++) {
		const char *s = in[i] ? (*env)->GetStringUTFChars(env, in[i], NULL) : NULL;
		free(trust[i]);
		trust[i] = s ? strdup(s) : NULL;
		if (s)
			(*env)->ReleaseStringUTFChars(env, in[i], s);
	}
	pthread_mutex_unlock(&mu);
	return (*env)->NewStringUTF(env, "Success");
}

JNIEXPORT jstring JNICALL P(readFromFd)(JNIEnv *env, jobject thiz, jint fd, jstring mime)
{
	ORIGFN(jstring, readFromFdWithTrustCheck, jstring, jstring, jstring, jint, jstring);
	jstring t[3];
	trust_strings(env, t);
	return o(env, thiz, t[0], t[1], t[2], fd, mime);
}

JNIEXPORT jstring JNICALL P(readFromFile)(JNIEnv *env, jobject thiz, jstring path)
{
	ORIGFN(jstring, readFromFileWithTrustCheck, jstring, jstring, jstring, jstring);
	jstring t[3];
	trust_strings(env, t);
	return o(env, thiz, t[0], t[1], t[2], path);
}

/* ---- methods the S23 library has: forwarded unchanged ---- */
JNIEXPORT jstring JNICALL P(readFromFdWithTrustCheck)(JNIEnv *env, jobject thiz, jstring a, jstring b, jstring c, jint fd, jstring mime)
{
	ORIGFN(jstring, readFromFdWithTrustCheck, jstring, jstring, jstring, jint, jstring);
	return o(env, thiz, a, b, c, fd, mime);
}

JNIEXPORT jstring JNICALL P(readFromFileWithTrustCheck)(JNIEnv *env, jobject thiz, jstring a, jstring b, jstring c, jstring path)
{
	ORIGFN(jstring, readFromFileWithTrustCheck, jstring, jstring, jstring, jstring);
	return o(env, thiz, a, b, c, path);
}

JNIEXPORT jbyteArray JNICALL P(createIngredientDataFromFd)(JNIEnv *env, jobject thiz, jint fd, jstring a, jstring b)
{
	ORIGFN(jbyteArray, createIngredientDataFromFd, jint, jstring, jstring);
	return o(env, thiz, fd, a, b);
}

JNIEXPORT jbyteArray JNICALL P(createIngredientDataFromFile)(JNIEnv *env, jobject thiz, jstring path)
{
	ORIGFN(jbyteArray, createIngredientDataFromFile, jstring);
	return o(env, thiz, path);
}

JNIEXPORT jstring JNICALL P(embedToFd)(JNIEnv *env, jobject thiz, jstring a, jstring b, jstring c, jint fd, jstring d, jstring e, jstring f, jobjectArray arr)
{
	ORIGFN(jstring, embedToFd, jstring, jstring, jstring, jint, jstring, jstring, jstring, jobjectArray);
	return o(env, thiz, a, b, c, fd, d, e, f, arr);
}

JNIEXPORT jstring JNICALL P(embedToFile)(JNIEnv *env, jobject thiz, jstring a, jstring b, jstring c, jstring d, jstring e, jobjectArray arr)
{
	ORIGFN(jstring, embedToFile, jstring, jstring, jstring, jstring, jstring, jobjectArray);
	return o(env, thiz, a, b, c, d, e, arr);
}

JNIEXPORT jstring JNICALL P(isC2paExist)(JNIEnv *env, jobject thiz, jstring path)
{
	ORIGFN(jstring, isC2paExist, jstring);
	return o(env, thiz, path);
}

JNIEXPORT jstring JNICALL P(isC2paExistFromFd)(JNIEnv *env, jobject thiz, jint fd, jstring mime)
{
	ORIGFN(jstring, isC2paExistFromFd, jint, jstring);
	return o(env, thiz, fd, mime);
}
