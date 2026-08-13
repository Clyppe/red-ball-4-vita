#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/display.h"
#include "utils/logger.h"
#include "utils/dialog.h"
#include "reimpl/controls.h"
#include "reimpl/sound.h"
#include "reimpl/prefs.h"
#include "reimpl/soomla.h"
#include "reimpl/video.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/touch.h>
#include <psp2/ctrl.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

int _newlib_heap_size_user = 192 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

so_module so_mod;

/* Cocos2d-x 2.x renderer / helper JNI (libcocos2dcpp.so). */
typedef void (*fn_set_apk)(JNIEnv *env, jobject thiz, jstring apkPath);
typedef void (*fn_init)(JNIEnv *env, jobject thiz, jint w, jint h);
typedef void (*fn_void)(JNIEnv *env, jobject thiz);
typedef void (*fn_touch)(JNIEnv *env, jobject thiz, jint id, jfloat x, jfloat y);
typedef void (*fn_touch_arr)(JNIEnv *env, jobject thiz, jintArray ids, jfloatArray xs, jfloatArray ys);
typedef void (*fn_key)(JNIEnv *env, jobject thiz, jint keyCode);
typedef void (*fn_bool_arg)(JNIEnv *env, jobject thiz, jboolean v);

static fn_set_apk nativeSetApkPath;
static fn_init nativeInit;
static fn_void nativeRender;
static fn_void nativeOnPause;
static fn_void nativeOnResume;
static fn_touch nativeTouchesBegin;
static fn_touch nativeTouchesEnd;
static fn_touch_arr nativeTouchesMove;
static fn_touch_arr nativeTouchesCancel;
static fn_key nativeKeyDown;
static fn_key controllerKeyPressed;
static fn_key controllerKeyReleased;
static fn_bool_arg rb_pureVersion;
static fn_bool_arg rb_adsON;
static fn_void rb_turnCloudOff;
static fn_void rb_hideLoading;
static fn_void rb_loadGameInfo;
static fn_void rb_refillLifes;
static fn_void rb_disableTransitionPause;

static char fake_renderer_obj = 1;
static jobject renderer_thiz = (jobject)&fake_renderer_obj;

static int screen_w = CTR_GAME_W;
static int screen_h = CTR_GAME_H;

extern jstring NewStringUTF(JNIEnv *env, const char *bytes);
extern jintArray NewIntArray(JNIEnv *env, jsize length);
extern jfloatArray NewFloatArray(JNIEnv *env, jsize length);
extern void SetIntArrayRegion(JNIEnv *env, jintArray array, jsize start, jsize len, const jint *buf);
extern void SetFloatArrayRegion(JNIEnv *env, jfloatArray array, jsize start, jsize len, const jfloat *buf);

static void log_line(const char *msg) {
    SceUID fd = sceIoOpen(DATA_PATH "loader.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, msg, (SceSize)strlen(msg));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static void bind_symbols(void) {
    nativeSetApkPath = (fn_set_apk)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetApkPath");
    nativeInit = (fn_init)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit");
    nativeRender = (fn_void)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender");
    nativeOnPause = (fn_void)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnPause");
    nativeOnResume = (fn_void)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeOnResume");
    nativeTouchesBegin = (fn_touch)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin");
    nativeTouchesEnd = (fn_touch)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd");
    nativeTouchesMove = (fn_touch_arr)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove");
    nativeTouchesCancel = (fn_touch_arr)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesCancel");
    nativeKeyDown = (fn_key)so_symbol(&so_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown");
    controllerKeyPressed = (fn_key)so_symbol(&so_mod, "Java_com_FDGEntertainment_redball4_gp_ControllerView_keyPressed");
    controllerKeyReleased = (fn_key)so_symbol(&so_mod, "Java_com_FDGEntertainment_redball4_gp_ControllerView_keyReleased");
    rb_pureVersion = (fn_bool_arg)so_symbol(&so_mod, "Java_com_FDGEntertainment_redball4_gp_RedBall4_pureVersion");
    rb_adsON = (fn_bool_arg)so_symbol(&so_mod, "Java_com_FDGEntertainment_redball4_gp_RedBall4_adsON");
    rb_turnCloudOff = (fn_void)so_symbol(&so_mod, "Java_com_FDGEntertainment_redball4_gp_RedBall4_turnCloudOff");
    rb_hideLoading = (fn_void)so_symbol(&so_mod, "Java_com_FDGEntertainment_redball4_gp_RedBall4_hideLoading");
    rb_loadGameInfo = (fn_void)so_symbol(&so_mod, "Java_com_FDGEntertainment_redball4_gp_RedBall4_loadGameInfo");
    rb_refillLifes = (fn_void)so_symbol(&so_mod, "Java_com_FDGEntertainment_redball4_gp_RedBall4_refillLifes");
    rb_disableTransitionPause = (fn_void)so_symbol(
        &so_mod, "Java_com_FDGEntertainment_redball4_gp_RedBall4_disableTransitionPause");
}

void controls_handler_key(int32_t keycode, ControlsAction action) {
    if (action == CONTROLS_ACTION_DOWN) {
        if (controllerKeyPressed)
            controllerKeyPressed(&jni, renderer_thiz, keycode);
        if (keycode == AKEYCODE_BACK && nativeKeyDown)
            nativeKeyDown(&jni, renderer_thiz, keycode);
        if (keycode == AKEYCODE_BUTTON_B && nativeKeyDown)
            nativeKeyDown(&jni, renderer_thiz, AKEYCODE_BACK);
    } else if (action == CONTROLS_ACTION_UP) {
        if (controllerKeyReleased)
            controllerKeyReleased(&jni, renderer_thiz, keycode);
    }
}

static void send_touch_move(jint id, jfloat x, jfloat y) {
    if (!nativeTouchesMove)
        return;
    jint ids[1] = { id };
    jfloat xs[1] = { x };
    jfloat ys[1] = { y };
    jintArray ja = NewIntArray(&jni, 1);
    jfloatArray xa = NewFloatArray(&jni, 1);
    jfloatArray ya = NewFloatArray(&jni, 1);
    if (!ja || !xa || !ya)
        return;
    SetIntArrayRegion(&jni, ja, 0, 1, ids);
    SetFloatArrayRegion(&jni, xa, 0, 1, xs);
    SetFloatArrayRegion(&jni, ya, 0, 1, ys);
    nativeTouchesMove(&jni, renderer_thiz, ja, xa, ya);
}

void controls_handler_touch(int32_t id, float x, float y, ControlsAction action) {
    if (id < 0)
        id = 0;
    if (action == CONTROLS_ACTION_DOWN) {
        if (nativeTouchesBegin)
            nativeTouchesBegin(&jni, renderer_thiz, id, x, y);
    } else if (action == CONTROLS_ACTION_UP) {
        if (nativeTouchesEnd)
            nativeTouchesEnd(&jni, renderer_thiz, id, x, y);
    } else if (action == CONTROLS_ACTION_MOVE) {
        send_touch_move(id, x, y);
    }
}

void controls_handler_touch_flush(void) {
}

void controls_handler_analog(ControlsStickId which, float x, float y, ControlsAction action) {
    (void)action;
    if (which != CONTROLS_STICK_LEFT)
        return;

    static int last_x = 0;
    static int last_y = 0;
    int nx = 0, ny = 0;
    if (x < -0.45f) nx = -1;
    else if (x > 0.45f) nx = 1;
    if (y < -0.45f) ny = -1;
    else if (y > 0.45f) ny = 1;

    if (nx != last_x) {
        if (last_x < 0) controls_handler_key(AKEYCODE_DPAD_LEFT, CONTROLS_ACTION_UP);
        if (last_x > 0) controls_handler_key(AKEYCODE_DPAD_RIGHT, CONTROLS_ACTION_UP);
        if (nx < 0) controls_handler_key(AKEYCODE_DPAD_LEFT, CONTROLS_ACTION_DOWN);
        if (nx > 0) controls_handler_key(AKEYCODE_DPAD_RIGHT, CONTROLS_ACTION_DOWN);
        last_x = nx;
    }
    if (ny != last_y) {
        if (last_y < 0) controls_handler_key(AKEYCODE_DPAD_UP, CONTROLS_ACTION_UP);
        if (last_y > 0) controls_handler_key(AKEYCODE_DPAD_DOWN, CONTROLS_ACTION_UP);
        if (ny < 0) controls_handler_key(AKEYCODE_DPAD_UP, CONTROLS_ACTION_DOWN);
        if (ny > 0) controls_handler_key(AKEYCODE_DPAD_DOWN, CONTROLS_ACTION_DOWN);
        last_y = ny;
    }
}

static int file_ok(const char *path) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    sceIoClose(fd);
    return 1;
}

static void *game_thread(void *arg) {
    (void)arg;
    log_line("thread: start");

    log_line("thread: calling soloader_init_all");
    soloader_init_all();
    log_line("thread: soloader_init_all ok");

    bind_symbols();
    log_line("thread: symbols bound");
    sound_init();
    log_line("thread: sound_init ok");

    if (!nativeInit || !nativeRender) {
        fatal_error("Red Ball 4: required JNI symbols missing from libcocos2dcpp.so");
    }

    int (*JNI_OnLoad)(void *jvm, void *reserved) =
        (void *)so_symbol(&so_mod, "JNI_OnLoad");
    if (JNI_OnLoad) {
        log_line("thread: JNI_OnLoad...");
        JNI_OnLoad(&jvm, NULL);
        log_line("thread: JNI_OnLoad ok");
    }

    if (nativeSetApkPath) {
        const char *apk = DATA_PATH "game.apk";
        if (!file_ok(apk))
            apk = DATA_PATH "redball4.apk";
        log_line("thread: nativeSetApkPath...");
        log_line(apk);
        nativeSetApkPath(&jni, renderer_thiz, NewStringUTF(&jni, apk));
        log_line("thread: nativeSetApkPath ok");
    } else {
        log_line("WARN: nativeSetApkPath missing");
    }

    log_line("thread: gl_init...");
    gl_init();
    display_layout_init();
    log_line("thread: gl_init ok");

    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    log_line("thread: nativeInit...");
    nativeInit(&jni, renderer_thiz, screen_w, screen_h);
    log_line("thread: nativeInit ok");
    display_apply_default_viewport();

    if (rb_pureVersion)
        rb_pureVersion(&jni, renderer_thiz, JNI_TRUE);
    if (rb_adsON)
        rb_adsON(&jni, renderer_thiz, JNI_FALSE);
    if (rb_turnCloudOff)
        rb_turnCloudOff(&jni, renderer_thiz);
    if (rb_loadGameInfo)
        rb_loadGameInfo(&jni, renderer_thiz);
    if (rb_refillLifes)
        rb_refillLifes(&jni, renderer_thiz);
    if (rb_hideLoading)
        rb_hideLoading(&jni, renderer_thiz);
    /* Avoid director-pause during scene transitions (FlipY exit looked frozen/wobbly). */
    if (rb_disableTransitionPause)
        rb_disableTransitionPause(&jni, renderer_thiz);
    log_line("thread: FDG config ok");

    if (nativeOnResume)
        nativeOnResume(&jni, renderer_thiz);

    log_line("thread: entering loop");

    while (1) {
        controls_poll();
        prefs_flush();
        if (video_is_playing()) {
            video_frame();
            gl_swap();
            continue;
        }
        display_apply_default_viewport();
        nativeRender(&jni, renderer_thiz);
        gl_swap();
    }

    return NULL;
}

int main(void) {
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir(DATA_PATH, 0777);
    sceIoMkdir(DATA_PATH "cache", 0777);
    sceIoMkdir(DATA_PATH "assets", 0777);
    sceIoMkdir(DATA_PATH "save", 0777);

    SceUID fd = sceIoOpen(DATA_PATH "loader.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd >= 0)
        sceIoClose(fd);

    log_line("RB4BOOT-v19-fontpremul");
    prefs_init();
    soomla_init();
    video_init();
    log_line("main: before pthread");

    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
    if (pthread_create(&th, &attr, game_thread, NULL) != 0) {
        log_line("main: pthread_create FAILED, running on main");
        game_thread(NULL);
    } else {
        log_line("main: pthread_create ok, joining");
        pthread_join(th, NULL);
    }

    return 0;
}
