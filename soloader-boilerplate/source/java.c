#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI_Logger.h>
#include <falso_jni/FalsoJNI_ImplBridge.h>
#include "reimpl/sound.h"
#include "reimpl/bitmap_text.h"
#include "reimpl/prefs.h"
#include "reimpl/soomla.h"
#include "reimpl/video.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * Java-side stubs for Cocos2d-x 2.x + FDG Red Ball 4.
 * Native code looks up methods by name via FalsoJNI (class is ignored).
 */

extern jstring NewStringUTF(JNIEnv *env, const char *bytes);
extern const char *GetStringUTFChars(JNIEnv *env, jstring string, jboolean *isCopy);
extern void ReleaseStringUTFChars(JNIEnv *env, jstring string, const char *utf);

static char fake_files_dir = 10;
static char fake_asset_mgr = 11;

static jstring jstr(const char *s) {
    return NewStringUTF(&jni, s ? s : "");
}

static void noop_void(jmethodID id, va_list args) {
    (void)id;
    (void)args;
}

static jboolean return_false(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return JNI_FALSE;
}

static jboolean return_true(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return JNI_TRUE;
}

static jint return_dpi(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return 160;
}

static jfloat return_one_f(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return 1.0f;
}

static jobject getPackageName(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return jstr("com.FDGEntertainment.redball4.gp");
}

static jobject getWritablePath(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return jstr(DATA_PATH "save/");
}

static jobject getFilesDir(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return (jobject)&fake_files_dir;
}

static jobject getAbsolutePath(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return jstr(DATA_PATH "save/");
}

static jobject getCurrentLanguage(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return jstr("en");
}

static jobject getDeviceModel(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return jstr("Pixel 2");
}

static jobject getAssetManager(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return (jobject)&fake_asset_mgr;
}

static jobject getStringWithEllipsis(jmethodID id, va_list args) {
    (void)id;
    jobject src = va_arg(args, jobject);
    const char *s = src ? GetStringUTFChars(&jni, src, NULL) : "";
    jstring out = jstr(s);
    if (src)
        ReleaseStringUTFChars(&jni, src, s);
    return out;
}

static void playBackgroundMusic(jmethodID id, va_list args) {
    (void)id;
    jobject path_obj = va_arg(args, jobject);
    const char *name = path_obj ? GetStringUTFChars(&jni, path_obj, NULL) : NULL;
    if (name) {
        sound_play_music(name);
        ReleaseStringUTFChars(&jni, path_obj, name);
    }
}

static void stopBackgroundMusic(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    sound_stop_music();
}

static void pauseBackgroundMusic(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    sound_pause_music();
}

static void resumeBackgroundMusic(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    sound_resume_music();
}

static void rewindBackgroundMusic(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    sound_resume_music();
}

static jboolean isBackgroundMusicPlaying(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return sound_is_music_playing() ? JNI_TRUE : JNI_FALSE;
}

static jint playEffect(jmethodID id, va_list args) {
    (void)id;
    jobject path_obj = va_arg(args, jobject);
    jint loop = va_arg(args, jint);
    const char *name = path_obj ? GetStringUTFChars(&jni, path_obj, NULL) : NULL;
    jint sid = 0;
    if (name) {
        sid = (jint)sound_play_effect(name, loop ? 1 : 0);
        ReleaseStringUTFChars(&jni, path_obj, name);
    }
    return sid;
}

static void stopEffect(jmethodID id, va_list args) {
    (void)id;
    jint sid = va_arg(args, jint);
    sound_stop((int)sid, 0);
}

static void preloadEffect(jmethodID id, va_list args) {
    (void)id;
    jobject path_obj = va_arg(args, jobject);
    const char *name = path_obj ? GetStringUTFChars(&jni, path_obj, NULL) : NULL;
    if (name) {
        sound_preload(name);
        ReleaseStringUTFChars(&jni, path_obj, name);
    }
}

static void preloadBackgroundMusic(jmethodID id, va_list args) {
    (void)id;
    (void)args;
}

static void audioEnd(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    sound_stop_all();
    sound_stop_music();
}

static void stopAllEffectsStub(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    sound_stop_all();
}

static void terminateProcess(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    sceKernelExitProcess(0);
}

static void showDialog(jmethodID id, va_list args) {
    (void)id;
    jobject title_obj = va_arg(args, jobject);
    jobject msg_obj = va_arg(args, jobject);
    const char *title = title_obj ? GetStringUTFChars(&jni, title_obj, NULL) : "";
    const char *msg = msg_obj ? GetStringUTFChars(&jni, msg_obj, NULL) : "";
    char line[512];
    snprintf(line, sizeof(line), "showDialog [%s] %s", title ? title : "", msg ? msg : "");
    SceUID fd = sceIoOpen(DATA_PATH "loader.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, line, (SceSize)strlen(line));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
    if (title_obj && title)
        ReleaseStringUTFChars(&jni, title_obj, (char *)title);
    if (msg_obj && msg)
        ReleaseStringUTFChars(&jni, msg_obj, (char *)msg);
}

static const char *arg_jstr(va_list *args, jobject *out) {
    jobject obj = va_arg(*args, jobject);
    *out = obj;
    if (!obj)
        return NULL;
    return GetStringUTFChars(&jni, obj, NULL);
}

static jboolean java_getBoolForKey(jmethodID id, va_list args) {
    (void)id;
    jobject key_obj;
    const char *key = arg_jstr(&args, &key_obj);
    jint def = va_arg(args, jint);
    int v = prefs_get_bool(key ? key : "", def);
    if (key)
        ReleaseStringUTFChars(&jni, key_obj, (char *)key);
    return v ? JNI_TRUE : JNI_FALSE;
}

static void java_setBoolForKey(jmethodID id, va_list args) {
    (void)id;
    jobject key_obj;
    const char *key = arg_jstr(&args, &key_obj);
    jint val = va_arg(args, jint);
    prefs_set_bool(key ? key : "", val);
    if (key)
        ReleaseStringUTFChars(&jni, key_obj, (char *)key);
}

static jint java_getIntegerForKey(jmethodID id, va_list args) {
    (void)id;
    jobject key_obj;
    const char *key = arg_jstr(&args, &key_obj);
    jint def = va_arg(args, jint);
    jint v = (jint)prefs_get_int(key ? key : "", def);
    if (key)
        ReleaseStringUTFChars(&jni, key_obj, (char *)key);
    return v;
}

static void java_setIntegerForKey(jmethodID id, va_list args) {
    (void)id;
    jobject key_obj;
    const char *key = arg_jstr(&args, &key_obj);
    jint val = va_arg(args, jint);
    prefs_set_int(key ? key : "", val);
    if (key)
        ReleaseStringUTFChars(&jni, key_obj, (char *)key);
}

static jfloat java_getFloatForKey(jmethodID id, va_list args) {
    (void)id;
    jobject key_obj;
    const char *key = arg_jstr(&args, &key_obj);
    double def = va_arg(args, double);
    jfloat v = (jfloat)prefs_get_float(key ? key : "", (float)def);
    if (key)
        ReleaseStringUTFChars(&jni, key_obj, (char *)key);
    return v;
}

static void java_setFloatForKey(jmethodID id, va_list args) {
    (void)id;
    jobject key_obj;
    const char *key = arg_jstr(&args, &key_obj);
    double val = va_arg(args, double);
    prefs_set_float(key ? key : "", (float)val);
    if (key)
        ReleaseStringUTFChars(&jni, key_obj, (char *)key);
}

static jdouble java_getDoubleForKey(jmethodID id, va_list args) {
    (void)id;
    jobject key_obj;
    const char *key = arg_jstr(&args, &key_obj);
    double def = va_arg(args, double);
    jdouble v = (jdouble)prefs_get_float(key ? key : "", (float)def);
    if (key)
        ReleaseStringUTFChars(&jni, key_obj, (char *)key);
    return v;
}

static void java_setDoubleForKey(jmethodID id, va_list args) {
    (void)id;
    jobject key_obj;
    const char *key = arg_jstr(&args, &key_obj);
    double val = va_arg(args, double);
    prefs_set_float(key ? key : "", (float)val);
    if (key)
        ReleaseStringUTFChars(&jni, key_obj, (char *)key);
}

static jobject java_getStringForKey(jmethodID id, va_list args) {
    (void)id;
    jobject key_obj;
    const char *key = arg_jstr(&args, &key_obj);
    const char *v = prefs_get_string(key ? key : "", "");
    jobject out = jstr(v);
    if (key)
        ReleaseStringUTFChars(&jni, key_obj, (char *)key);
    return out;
}

static void java_setStringForKey(jmethodID id, va_list args) {
    (void)id;
    jobject key_obj, val_obj;
    const char *key = arg_jstr(&args, &key_obj);
    const char *val = arg_jstr(&args, &val_obj);
    prefs_set_string(key ? key : "", val ? val : "");
    if (key)
        ReleaseStringUTFChars(&jni, key_obj, (char *)key);
    if (val)
        ReleaseStringUTFChars(&jni, val_obj, (char *)val);
}

static jobject java_receiveCppMessage(jmethodID id, va_list args) {
    (void)id;
    jobject json_obj = va_arg(args, jobject);
    const char *json = json_obj ? GetStringUTFChars(&jni, json_obj, NULL) : "{}";
    char *ret = soomla_receive(json ? json : "{}");
    jobject out = jstr(ret ? ret : "{}");
    free(ret);
    if (json_obj && json)
        ReleaseStringUTFChars(&jni, json_obj, (char *)json);
    return out;
}

static jobject getCurrentCountry(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return jstr("US");
}

NameToMethodID nameToMethodId[] = {
    { 1, "getCocos2dxPackageName", METHOD_TYPE_OBJECT },
    { 2, "getPackageName", METHOD_TYPE_OBJECT },
    { 3, "getCocos2dxWritablePath", METHOD_TYPE_OBJECT },
    { 4, "getFileDirectory", METHOD_TYPE_OBJECT },
    { 5, "getWritablePath", METHOD_TYPE_OBJECT },
    { 6, "getFilesDir", METHOD_TYPE_OBJECT },
    { 7, "getAbsolutePath", METHOD_TYPE_OBJECT },
    { 8, "getCurrentLanguage", METHOD_TYPE_OBJECT },
    { 9, "getDeviceModel", METHOD_TYPE_OBJECT },
    { 10, "getAssetManager", METHOD_TYPE_OBJECT },
    { 11, "getStringWithEllipsis", METHOD_TYPE_OBJECT },
    { 12, "getDPI", METHOD_TYPE_INT },
    { 13, "getDPIJNI", METHOD_TYPE_INT },
    { 14, "enableAccelerometer", METHOD_TYPE_VOID },
    { 15, "disableAccelerometer", METHOD_TYPE_VOID },
    { 16, "setAccelerometerInterval", METHOD_TYPE_VOID },
    { 17, "showDialog", METHOD_TYPE_VOID },
    { 18, "showEditTextDialog", METHOD_TYPE_VOID },
    { 19, "terminateProcess", METHOD_TYPE_VOID },
    { 20, "playBackgroundMusic", METHOD_TYPE_VOID },
    { 21, "stopBackgroundMusic", METHOD_TYPE_VOID },
    { 22, "pauseBackgroundMusic", METHOD_TYPE_VOID },
    { 23, "resumeBackgroundMusic", METHOD_TYPE_VOID },
    { 24, "rewindBackgroundMusic", METHOD_TYPE_VOID },
    { 25, "isBackgroundMusicPlaying", METHOD_TYPE_BOOLEAN },
    { 26, "preloadBackgroundMusic", METHOD_TYPE_VOID },
    { 27, "playEffect", METHOD_TYPE_INT },
    { 28, "stopEffect", METHOD_TYPE_VOID },
    { 29, "preloadEffect", METHOD_TYPE_VOID },
    { 30, "unloadEffect", METHOD_TYPE_VOID },
    { 31, "pauseEffect", METHOD_TYPE_VOID },
    { 90, "pauseAllEffects", METHOD_TYPE_VOID },
    { 91, "stopAllEffects", METHOD_TYPE_VOID },
    { 32, "end", METHOD_TYPE_VOID },
    { 33, "setBackgroundMusicVolume", METHOD_TYPE_VOID },
    { 34, "setEffectsVolume", METHOD_TYPE_VOID },
    { 35, "getBackgroundMusicVolume", METHOD_TYPE_FLOAT },
    { 36, "getEffectsVolume", METHOD_TYPE_FLOAT },
    { 37, "showAd", METHOD_TYPE_VOID },
    { 38, "showAdsTestActivity", METHOD_TYPE_VOID },
    { 39, "playVideo", METHOD_TYPE_VOID },
    { 40, "playNewEpisodeVideo", METHOD_TYPE_VOID },
    { 41, "playRefillVideo", METHOD_TYPE_VOID },
    { 42, "rateApp", METHOD_TYPE_VOID },
    { 43, "openURL", METHOD_TYPE_BOOLEAN },
    { 44, "refillVideoAvailable", METHOD_TYPE_BOOLEAN },
    { 45, "createTextBitmapShadowStroke", METHOD_TYPE_VOID },
    { 46, "hideLoading", METHOD_TYPE_VOID },
    { 47, "exitApp", METHOD_TYPE_VOID },
    { 48, "mkdir", METHOD_TYPE_BOOLEAN },
    { 49, "exists", METHOD_TYPE_BOOLEAN },
    { 50, "isTV", METHOD_TYPE_BOOLEAN },
    { 51, "isTab4", METHOD_TYPE_BOOLEAN },
    { 52, "isSignedIn", METHOD_TYPE_BOOLEAN },
    { 53, "isAvailable", METHOD_TYPE_BOOLEAN },
    { 54, "isOldAndroidVersion", METHOD_TYPE_BOOLEAN },
    { 55, "allPermissionsGranted", METHOD_TYPE_BOOLEAN },
    { 56, "isAchievementUnlocked", METHOD_TYPE_BOOLEAN },
    { 57, "hideAd", METHOD_TYPE_VOID },
    { 58, "initAd", METHOD_TYPE_VOID },
    { 59, "showMoreGames", METHOD_TYPE_VOID },
    { 60, "showBootupAd", METHOD_TYPE_VOID },
    { 61, "showAfterLevel", METHOD_TYPE_VOID },
    { 62, "showLeaderboard", METHOD_TYPE_VOID },
    { 63, "showAchievements", METHOD_TYPE_VOID },
    { 64, "showLeaderboards", METHOD_TYPE_VOID },
    { 65, "signIn", METHOD_TYPE_VOID },
    { 66, "signOut", METHOD_TYPE_VOID },
    { 67, "enableAppSleep", METHOD_TYPE_VOID },
    { 68, "disableAppSleep", METHOD_TYPE_VOID },
    { 69, "getBoolForKey", METHOD_TYPE_BOOLEAN },
    { 70, "setBoolForKey", METHOD_TYPE_VOID },
    { 71, "getIntegerForKey", METHOD_TYPE_INT },
    { 72, "setIntegerForKey", METHOD_TYPE_VOID },
    { 73, "getFloatForKey", METHOD_TYPE_FLOAT },
    { 74, "setFloatForKey", METHOD_TYPE_VOID },
    { 75, "getDoubleForKey", METHOD_TYPE_DOUBLE },
    { 76, "setDoubleForKey", METHOD_TYPE_VOID },
    { 77, "getStringForKey", METHOD_TYPE_OBJECT },
    { 78, "setStringForKey", METHOD_TYPE_VOID },
    { 79, "bind", METHOD_TYPE_VOID },
    { 80, "receiveCppMessage", METHOD_TYPE_OBJECT },
    { 81, "isGoogleServicesAvailable", METHOD_TYPE_BOOLEAN },
    { 82, "getCurrentCountry", METHOD_TYPE_OBJECT },
    { 83, "setAnimationInterval", METHOD_TYPE_VOID },
    { 84, "resumeAllEffects", METHOD_TYPE_VOID },
    { 85, "cancelReminder", METHOD_TYPE_VOID },
    { 92, "saveToCloudStatic", METHOD_TYPE_VOID },
    { 86, "appReturnedFromClosedAd", METHOD_TYPE_BOOLEAN },
    { 87, "appReturnedFromClosedVideo", METHOD_TYPE_BOOLEAN },
    { 88, "appReturnedFromClosedPermission", METHOD_TYPE_BOOLEAN },
    { 89, "loadAchievements", METHOD_TYPE_VOID },
};

MethodsBoolean methodsBoolean[] = {
    { 25, isBackgroundMusicPlaying },
    { 43, return_false },
    { 44, return_false },
    { 48, return_true },
    { 49, return_true },
    { 50, return_false }, /* isTV */
    { 51, return_false }, /* isTab4 */
    { 52, return_false },
    { 53, return_false },
    { 54, return_false },
    { 55, return_true },
    { 56, return_false },
    { 69, java_getBoolForKey },
    { 81, return_false }, /* isGoogleServicesAvailable */
    { 86, java_appReturnedFromClosedAd },
    { 87, java_appReturnedFromClosedVideo },
    { 88, java_appReturnedFromClosedPermission },
};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {
    { 75, java_getDoubleForKey },
};
MethodsFloat methodsFloat[] = {
    { 35, return_one_f },
    { 36, return_one_f },
    { 73, java_getFloatForKey },
};
MethodsInt methodsInt[] = {
    { 12, return_dpi },
    { 13, return_dpi },
    { 27, playEffect },
    { 71, java_getIntegerForKey },
};
MethodsLong methodsLong[] = {};
MethodsObject methodsObject[] = {
    { 1, getPackageName },
    { 2, getPackageName },
    { 3, getWritablePath },
    { 4, getWritablePath },
    { 5, getWritablePath },
    { 6, getFilesDir },
    { 7, getAbsolutePath },
    { 8, getCurrentLanguage },
    { 9, getDeviceModel },
    { 10, getAssetManager },
    { 11, getStringWithEllipsis },
    { 77, java_getStringForKey },
    { 80, java_receiveCppMessage },
    { 82, getCurrentCountry },
};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {
    { 14, noop_void },
    { 15, noop_void },
    { 16, noop_void },
    { 17, showDialog },
    { 18, noop_void },
    { 19, terminateProcess },
    { 20, playBackgroundMusic },
    { 21, stopBackgroundMusic },
    { 22, pauseBackgroundMusic },
    { 23, resumeBackgroundMusic },
    { 24, rewindBackgroundMusic },
    { 26, preloadBackgroundMusic },
    { 28, stopEffect },
    { 29, preloadEffect },
    { 30, noop_void },
    { 31, noop_void },
    { 90, noop_void }, /* pauseAllEffects */
    { 91, stopAllEffectsStub },
    { 32, audioEnd },
    { 33, noop_void },
    { 34, noop_void },
    { 37, java_showAd },
    { 38, noop_void },
    { 39, java_playVideo },
    { 40, java_playNewEpisodeVideo },
    { 41, java_playRefillVideo },
    { 42, noop_void },
    { 45, java_createTextBitmapShadowStroke },
    { 46, noop_void },
    { 47, terminateProcess },
    { 57, noop_void },
    { 58, noop_void },
    { 59, noop_void },
    { 60, java_showBootupAd },
    { 61, java_showAfterLevel },
    { 62, noop_void },
    { 63, noop_void },
    { 64, noop_void },
    { 65, noop_void },
    { 66, noop_void },
    { 67, noop_void },
    { 68, noop_void },
    { 70, java_setBoolForKey },
    { 72, java_setIntegerForKey },
    { 74, java_setFloatForKey },
    { 76, java_setDoubleForKey },
    { 78, java_setStringForKey },
    { 79, noop_void }, /* Soomla NdkGlue.bind */
    { 83, noop_void },
    { 84, noop_void },
    { 85, noop_void },
    { 89, noop_void },
    { 92, noop_void }, /* saveToCloudStatic — Game::save used to miss this and spam JNI errors */
};

char WINDOW_SERVICE[] = "window";
const int SDK_INT = 19;

NameToFieldID nameToFieldId[] = {
    { 0, "WINDOW_SERVICE", FIELD_TYPE_OBJECT },
    { 1, "SDK_INT", FIELD_TYPE_INT },
};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = {
    { 1, SDK_INT },
};
FieldsObject fieldsObject[] = {
    { 0, WINDOW_SERVICE },
};
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES
