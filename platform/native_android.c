#ifdef __ANDROID__
#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>
#include <jni.h>
#include "platform/native_log.h"
#include "platform/native_android.h"
#include "platform/native_input.h"


void Platform_Android_PickFile(void) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return;
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!activity) return;
    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (!clazz) return;
    jmethodID methodId = (*env)->GetStaticMethodID(env, clazz, "pickFile", "()V");

    if (methodId) {
        (*env)->CallStaticVoidMethod(env, clazz, methodId);
    } else {
        Platform_LogError("[CTR Android] Failed to find pickFile method\n");
    }

    (*env)->DeleteLocalRef(env, activity);
    (*env)->DeleteLocalRef(env, clazz);
}

char* Platform_Android_GetStoredPath(void) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return NULL;
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!activity) return NULL;
    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (!clazz) return NULL;
    jmethodID methodId = (*env)->GetStaticMethodID(env, clazz, "getStoredAssetPath", "()Ljava/lang/String;");

    char* result = NULL;
    if (methodId) {
        jstring jPath = (jstring)(*env)->CallStaticObjectMethod(env, clazz, methodId);
        if (jPath) {
            const char* pathChars = (*env)->GetStringUTFChars(env, jPath, NULL);
            if (pathChars) {
                result = SDL_strdup(pathChars);
                (*env)->ReleaseStringUTFChars(env, jPath, pathChars);
            }
            (*env)->DeleteLocalRef(env, jPath);
        }
    }

    (*env)->DeleteLocalRef(env, activity);
    (*env)->DeleteLocalRef(env, clazz);
    return result;
}

int Platform_Android_IsPickerActive(void) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return 0;
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!activity) return 0;
    jclass clazz = (*env)->GetObjectClass(env, activity);
    if (!clazz) return 0;
    jmethodID methodId = (*env)->GetStaticMethodID(env, clazz, "isPickerActive", "()Z");

    jboolean result = JNI_FALSE;
    if (methodId) {
        result = (*env)->CallStaticBooleanMethod(env, clazz, methodId);
    }

    (*env)->DeleteLocalRef(env, activity);
    (*env)->DeleteLocalRef(env, clazz);
    return result == JNI_TRUE;
}

void Platform_Android_ApplyTouchButtons(int slot, u16 buttons) {
    Platform_InputApplyTouchButtons(slot, buttons);
}

JNIEXPORT void JNICALL Java_com_ctrnative_CTRNativeActivity_nativeApplyTouchButtons(JNIEnv* env, jclass clazz, jint slot, jint buttons) {
    (void)env;
    (void)clazz;
    Platform_Android_ApplyTouchButtons(slot, (u16)buttons);
}

#include "platform/native_texture_mod.h"

extern int g_cfg_bilinearFiltering;
extern int g_cfg_filterMode;

JNIEXPORT void JNICALL Java_com_ctrnative_SettingsActivity_applyBilinearFilter(JNIEnv* env, jclass clazz, jint enabled) {
    (void)env;
    (void)clazz;
    g_cfg_bilinearFiltering = enabled;
    Platform_LogWarn("[CTR Native] Bilinear filter set to: %d\n", g_cfg_bilinearFiltering);
}

JNIEXPORT void JNICALL Java_com_ctrnative_SettingsActivity_applyFilterMode(JNIEnv* env, jclass clazz, jint mode) {
    (void)env;
    (void)clazz;
    g_cfg_filterMode = mode;
    g_cfg_bilinearFiltering = (mode > 0) ? 1 : 0;
    Platform_LogWarn("[CTR Native] Filter mode set to: %d (0=nearest, 1=bilinear, 2=bicubic)\n", g_cfg_filterMode);
}

JNIEXPORT void JNICALL Java_com_ctrnative_SettingsActivity_applyTextureDump(JNIEnv* env, jclass clazz, jint enabled) {
    (void)env;
    (void)clazz;
    NativeTextureMod_SetDumpEnabled(enabled != 0);
    Platform_LogWarn("[CTR Native] Texture dump set to: %d\n", enabled);
}

JNIEXPORT void JNICALL Java_com_ctrnative_SettingsActivity_applyTextureLoad(JNIEnv* env, jclass clazz, jint enabled) {
    (void)env;
    (void)clazz;
    NativeTextureMod_SetReplacementEnabled(enabled != 0);
    Platform_LogWarn("[CTR Native] Texture load set to: %d\n", enabled);
}

JNIEXPORT void JNICALL Java_com_ctrnative_SettingsActivity_reloadTextures(JNIEnv* env, jclass clazz) {
    (void)env;
    (void)clazz;
    NativeTextureMod_ReloadTextures();
    Platform_LogWarn("[CTR Native] Reloading custom textures from crash/textura/load\n");
}

#endif
