/*
 * Cutscene playback via sceAvPlayer (vitaGL video_playback sample pattern).
 * Ads / refill clips still skip and signal completion.
 */

#include "reimpl/video.h"
#include "reimpl/sound.h"
#include "utils/glutil.h"

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>
#include <vitaGL.h>

#include <psp2/avplayer.h>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern so_module so_mod;
extern const char *GetStringUTFChars(JNIEnv *env, jstring string, jboolean *isCopy);
extern void ReleaseStringUTFChars(JNIEnv *env, jstring string, char *utf);

typedef void (*fn_void)(JNIEnv *env, jobject thiz);

#define VIDEO_BUFFERS 5
#define PHYCONT_MEM_ALIGNMENT (1024 * 1024)
#define ALIGN_MEM(x, align) (((x) + ((align) - 1)) & ~((align) - 1))
#define SCREEN_W 960
#define SCREEN_H 544

enum {
    PLAYER_INACTIVE = 0,
    PLAYER_ACTIVE,
    PLAYER_PAUSED
};

static char fake_video_thiz = 77;
static fn_void native_on_over;
static fn_void native_unpause;

static int g_from_ad;
static int g_from_video;
static int g_from_permission;

static volatile int g_player_state = PLAYER_INACTIVE;
static SceAvPlayerHandle g_player;
static int g_av_inited;
static int g_first_frame;
static int g_frame_idx;
static int g_in_blocking_play;
static int g_logged_frame;

/* Hardware YUV path (vitaGL video_playback) — needs ENABLE_LEGACY_PIPELINE=1. */
static GLuint g_tex[VIDEO_BUFFERS];
static SceGxmTexture *g_gxm_tex[VIDEO_BUFFERS];
static float *g_draw_attr;
static int g_draw_ready;
static SceUID g_audio_thid = -1;

static void vlog(const char *msg) {
    SceUID fd = sceIoOpen(DATA_PATH "loader.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, msg, (SceSize)strlen(msg));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static void resolve(void) {
    if (!native_on_over) {
        native_on_over = (fn_void)so_symbol(
            &so_mod, "Java_com_FDGEntertainment_redball4_gp_Cocos2dxVideo_onOver");
        native_unpause = (fn_void)so_symbol(
            &so_mod, "Java_com_FDGEntertainment_redball4_gp_RedBall4_unPause");
    }
}

static void *alloc_for_cpu(void *p, uint32_t align, uint32_t size) {
    (void)p;
    return memalign(align, size);
}

static void free_for_cpu(void *p, void *ptr) {
    (void)p;
    free(ptr);
}

static void *alloc_for_gpu(void *p, uint32_t align, uint32_t size) {
    (void)p;
    (void)align;
    size = ALIGN_MEM(size, PHYCONT_MEM_ALIGNMENT);
    SceUID blk = sceKernelAllocMemBlock("av_blk", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, size, NULL);
    if (blk < 0) {
        char line[128];
        snprintf(line, sizeof(line), "video: GPU alloc failed size=%u rc=%d", (unsigned)size, (int)blk);
        vlog(line);
        return NULL;
    }
    void *res = NULL;
    sceKernelGetMemBlockBase(blk, &res);
    sceGxmMapMemory(res, size, SCE_GXM_MEMORY_ATTRIB_RW);
    return res;
}

static void free_for_gpu(void *p, void *addr) {
    (void)p;
    glFinish();
    SceUID blk = sceKernelFindMemBlockByAddr(addr, 0);
    sceGxmUnmapMemory(addr);
    if (blk >= 0)
        sceKernelFreeMemBlock(blk);
}

static int audio_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    int audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 1024, 48000, SCE_AUDIO_OUT_MODE_STEREO);
    while (g_player_state != PLAYER_INACTIVE) {
        if (g_player && sceAvPlayerIsActive(g_player)) {
            SceAvPlayerFrameInfo frame;
            memset(&frame, 0, sizeof(frame));
            if (sceAvPlayerGetAudioData(g_player, &frame)) {
                sceAudioOutSetConfig(
                    audio_port, 1024, frame.details.audio.sampleRate,
                    frame.details.audio.channelCount == 1 ? SCE_AUDIO_OUT_MODE_MONO
                                                         : SCE_AUDIO_OUT_MODE_STEREO);
                sceAudioOutOutput(audio_port, frame.pData);
            } else {
                sceKernelDelayThread(1000);
            }
        } else {
            sceKernelDelayThread(1000);
        }
    }
    if (audio_port >= 0)
        sceAudioOutReleasePort(audio_port);
    return sceKernelExitDeleteThread(0);
}

void video_frame(void); /* used by blocking play before definition */

static void ensure_draw_geom(void) {
    if (g_draw_ready)
        return;
    g_draw_attr = (float *)malloc(sizeof(float) * 22);
    if (!g_draw_attr)
        return;
    g_draw_attr[0] = 0.0f;
    g_draw_attr[1] = 0.0f;
    g_draw_attr[2] = 0.0f;
    g_draw_attr[3] = (float)SCREEN_W;
    g_draw_attr[4] = 0.0f;
    g_draw_attr[5] = 0.0f;
    g_draw_attr[6] = 0.0f;
    g_draw_attr[7] = (float)SCREEN_H;
    g_draw_attr[8] = 0.0f;
    g_draw_attr[9] = (float)SCREEN_W;
    g_draw_attr[10] = (float)SCREEN_H;
    g_draw_attr[11] = 0.0f;
    g_draw_attr[12] = 0.0f;
    g_draw_attr[13] = 0.0f;
    g_draw_attr[14] = 1.0f;
    g_draw_attr[15] = 0.0f;
    g_draw_attr[16] = 0.0f;
    g_draw_attr[17] = 1.0f;
    g_draw_attr[18] = 1.0f;
    g_draw_attr[19] = 1.0f;
    uint16_t *idx = (uint16_t *)&g_draw_attr[20];
    idx[0] = 0;
    idx[1] = 1;
    idx[2] = 2;
    idx[3] = 3;
    /* Bind once like the vitaGL sample. */
    vglVertexPointerMapped(3, g_draw_attr);
    vglTexCoordPointerMapped(&g_draw_attr[12]);
    vglIndexPointerMapped(idx);
    g_draw_ready = 1;
}

static int ensure_player(void) {
    if (g_av_inited)
        return 1;
    sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);

    SceAvPlayerInitData init;
    memset(&init, 0, sizeof(init));
    init.memoryReplacement.allocate = alloc_for_cpu;
    init.memoryReplacement.deallocate = free_for_cpu;
    init.memoryReplacement.allocateTexture = alloc_for_gpu;
    init.memoryReplacement.deallocateTexture = free_for_gpu;
    init.basePriority = 0xA0;
    init.numOutputVideoFrameBuffers = VIDEO_BUFFERS;
    init.autoStart = SCE_TRUE;
    g_player = sceAvPlayerInit(&init);
    if (!g_player) {
        vlog("video: sceAvPlayerInit failed");
        return 0;
    }

    glGenTextures(VIDEO_BUFFERS, g_tex);
    for (int i = 0; i < VIDEO_BUFFERS; i++) {
        glBindTexture(GL_TEXTURE_2D, g_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        g_gxm_tex[i] = vglGetGxmTexture(GL_TEXTURE_2D);
        void *old = vglGetTexDataPointer(GL_TEXTURE_2D);
        if (old)
            vglFree(old);
    }
    ensure_draw_geom();
    g_av_inited = 1;
    vlog("video: AvPlayer ready (hw yuv)");
    return 1;
}

static int file_exists(const char *path) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    sceIoClose(fd);
    return 1;
}

static int resolve_path(const char *name, char *out, size_t n) {
    if (!name || !name[0])
        return 0;

    const char *base = name;
    const char *slash = strrchr(name, '/');
    if (slash)
        base = slash + 1;
    slash = strrchr(base, '\\');
    if (slash)
        base = slash + 1;

    char candidates[8][256];
    int c = 0;
    snprintf(candidates[c++], sizeof(candidates[0]), DATA_PATH "videos/%s", base);
    if (!strstr(base, ".mp4"))
        snprintf(candidates[c++], sizeof(candidates[0]), DATA_PATH "videos/%s.mp4", base);

    /* Android raw names vs C++ "intro.mp4" / "ending.mp4". */
    if (strcmp(base, "intro.mp4") == 0 || strcmp(base, "intro") == 0) {
        snprintf(candidates[c++], sizeof(candidates[0]), DATA_PATH "videos/intro1.mp4");
    }
    if (strcmp(base, "ending.mp4") == 0 || strcmp(base, "ending") == 0) {
        snprintf(candidates[c++], sizeof(candidates[0]), DATA_PATH "videos/ending.mp4");
        snprintf(candidates[c++], sizeof(candidates[0]), DATA_PATH "videos/intro5.mp4");
    }

    for (int i = 0; i < c; i++) {
        if (file_exists(candidates[i])) {
            snprintf(out, n, "%s", candidates[i]);
            return 1;
        }
    }
    snprintf(out, n, "%s", candidates[0]);
    return 0;
}

static void signal_finished(const char *why) {
    resolve();
    char line[192];
    snprintf(line, sizeof(line), "video: done %s -> onOver", why ? why : "?");
    vlog(line);
    g_from_video = 1;
    sound_resume_music();
    if (native_on_over)
        native_on_over(&jni, (jobject)&fake_video_thiz);
    else
        vlog("video: onOver symbol missing");
    if (native_unpause)
        native_unpause(&jni, (jobject)&fake_video_thiz);
}

static void stop_player(void) {
    if (g_player_state == PLAYER_INACTIVE)
        return;
    g_player_state = PLAYER_INACTIVE;
    if (g_player) {
        if (sceAvPlayerIsActive(g_player))
            sceAvPlayerStop(g_player);
    }
    /* audio thread exits when state is INACTIVE */
    if (g_audio_thid >= 0) {
        sceKernelWaitThreadEnd(g_audio_thid, NULL, NULL);
        g_audio_thid = -1;
    }
    g_first_frame = 0;
    g_logged_frame = 0;
    g_frame_idx = 0;
}

static void finish_and_notify(const char *why) {
    stop_player();
    signal_finished(why);
}

static void skip_no_file(const char *why) {
    char line[192];
    snprintf(line, sizeof(line), "video: missing file, skip %s", why ? why : "?");
    vlog(line);
    g_from_video = 1;
    resolve();
    if (native_on_over)
        native_on_over(&jni, (jobject)&fake_video_thiz);
    if (native_unpause)
        native_unpause(&jni, (jobject)&fake_video_thiz);
}

static int start_file(const char *path, const char *why) {
    if (!ensure_player()) {
        skip_no_file(why);
        return 0;
    }
    if (g_player_state != PLAYER_INACTIVE) {
        stop_player();
    }

    char line[256];
    snprintf(line, sizeof(line), "video: play %s (%s)", path, why ? why : "");
    vlog(line);

    sound_pause_music();
    g_first_frame = 0;
    g_logged_frame = 0;
    g_player_state = PLAYER_ACTIVE;

    g_audio_thid = sceKernelCreateThread("rb4_vid_audio", audio_thread, 0x10000100 - 10, 0x4000, 0, 0, NULL);
    if (g_audio_thid >= 0)
        sceKernelStartThread(g_audio_thid, 0, NULL);

    int rc = sceAvPlayerAddSource(g_player, path);
    snprintf(line, sizeof(line), "video: AddSource rc=%d active=%d", rc, sceAvPlayerIsActive(g_player));
    vlog(line);
    if (rc < 0) {
        stop_player();
        skip_no_file(why);
        return 0;
    }
    sceAvPlayerSetLooping(g_player, SCE_FALSE);

    /*
     * Pump frames here. nativeInit / level-complete often wait for onOver
     * before returning; the main loop cannot run until they do.
     */
    g_in_blocking_play = 1;
    int wait_frames = 0;
    while (g_player_state != PLAYER_INACTIVE) {
        video_frame();
        gl_swap();
        if (!g_first_frame) {
            wait_frames++;
            if (wait_frames == 30) {
                snprintf(line, sizeof(line), "video: still waiting active=%d",
                         g_player ? sceAvPlayerIsActive(g_player) : -1);
                vlog(line);
            }
            /* Fail before a long black freeze if decode never starts. */
            if (wait_frames > 60 * 4) {
                vlog("video: timeout waiting for first frame");
                finish_and_notify("timeout");
                break;
            }
        }
    }
    g_in_blocking_play = 0;
    vlog("video: blocking play returned");
    return 1;
}

static int start_named(const char *name, const char *why) {
    char path[256];
    if (!resolve_path(name, path, sizeof(path))) {
        skip_no_file(why);
        return 0;
    }
    return start_file(path, why);
}

void video_init(void) {
    g_from_ad = 0;
    g_from_video = 0;
    g_from_permission = 0;
    g_player_state = PLAYER_INACTIVE;
    sceIoMkdir(DATA_PATH "videos", 0777);
}

int video_is_playing(void) {
    return g_player_state != PLAYER_INACTIVE;
}

void video_frame(void) {
    if (g_player_state == PLAYER_INACTIVE)
        return;

    /* CIRCLE skips cutscene */
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);
    static uint32_t oldpad;
    if ((pad.buttons & SCE_CTRL_CIRCLE) && !(oldpad & SCE_CTRL_CIRCLE)) {
        oldpad = pad.buttons;
        finish_and_notify("skipped");
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    oldpad = pad.buttons;

    glViewport(0, 0, SCREEN_W, SCREEN_H);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glUseProgram(0);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof(0, SCREEN_W, SCREEN_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    if (g_player_state == PLAYER_ACTIVE && g_player) {
        if (sceAvPlayerIsActive(g_player)) {
            SceAvPlayerFrameInfo frame;
            memset(&frame, 0, sizeof(frame));
            if (sceAvPlayerGetVideoData(g_player, &frame) && frame.pData) {
                g_frame_idx = (g_frame_idx + 1) % VIDEO_BUFFERS;
                sceGxmTextureInitLinear(
                    g_gxm_tex[g_frame_idx], frame.pData, SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,
                    frame.details.video.width, frame.details.video.height, 0);
                sceGxmTextureSetMinFilter(g_gxm_tex[g_frame_idx], SCE_GXM_TEXTURE_FILTER_LINEAR);
                sceGxmTextureSetMagFilter(g_gxm_tex[g_frame_idx], SCE_GXM_TEXTURE_FILTER_LINEAR);
                g_first_frame = 1;
                if (!g_logged_frame) {
                    char line[96];
                    snprintf(line, sizeof(line), "video: first frame %ux%u (hw)",
                             (unsigned)frame.details.video.width,
                             (unsigned)frame.details.video.height);
                    vlog(line);
                    g_logged_frame = 1;
                }
            }
        } else if (g_first_frame) {
            finish_and_notify("eof");
            return;
        }
    }

    if (g_first_frame && g_draw_ready) {
        glBindTexture(GL_TEXTURE_2D, g_tex[g_frame_idx]);
        vglDrawObjects(GL_TRIANGLE_STRIP, 4);
    }
}

static void schedule_ad_return(const char *why) {
    char line[160];
    snprintf(line, sizeof(line), "ad: skip %s", why ? why : "?");
    vlog(line);
    g_from_ad = 1;
}

void java_playVideo(jmethodID id, va_list args) {
    (void)id;
    jobject path_obj = va_arg(args, jobject);
    const char *name = path_obj ? GetStringUTFChars(&jni, path_obj, NULL) : NULL;
    char why[192];
    snprintf(why, sizeof(why), "playVideo \"%s\"", name ? name : "?");
    start_named(name ? name : "", why);
    if (path_obj && name)
        ReleaseStringUTFChars(&jni, path_obj, (char *)name);
}

void java_playNewEpisodeVideo(jmethodID id, va_list args) {
    (void)id;
    jint episode = va_arg(args, jint);
    int ep = (int)episode;
    char fname[32];
    char path[256];
    /* APK has intro1 / intro4 / intro5. */
    snprintf(fname, sizeof(fname), "intro%d.mp4", ep);
    if (!resolve_path(fname, path, sizeof(path))) {
        if (ep <= 0 || ep == 2 || ep == 3)
            snprintf(fname, sizeof(fname), "intro1.mp4");
    }
    char why[64];
    snprintf(why, sizeof(why), "playNewEpisodeVideo %d", ep);
    start_named(fname, why);
}

void java_playRefillVideo(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    /* No local refill MP4 — treat as short ad skip. */
    skip_no_file("playRefillVideo");
}

void java_showAfterLevel(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    schedule_ad_return("showAfterLevel");
}

void java_showAd(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    schedule_ad_return("showAd");
}

void java_showBootupAd(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    schedule_ad_return("showBootupAd");
}

jboolean java_appReturnedFromClosedAd(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    if (g_from_ad) {
        g_from_ad = 0;
        vlog("ad: appReturnedFromClosedAd -> true");
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

jboolean java_appReturnedFromClosedVideo(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    if (g_from_video) {
        g_from_video = 0;
        vlog("video: appReturnedFromClosedVideo -> true");
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

jboolean java_appReturnedFromClosedPermission(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    if (g_from_permission) {
        g_from_permission = 0;
        return JNI_TRUE;
    }
    return JNI_FALSE;
}
