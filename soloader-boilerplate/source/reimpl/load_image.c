/*
 * Classic CTR loadImage(String, int) — BitmapFactory stand-in.
 * Decodes PNG/JPEG, packs Android ARGB_8888, calls native imageLoaded.
 */

#include "reimpl/load_image.h"

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include <psp2/io/fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "utils/stb_image.h"

extern so_module so_mod;
extern jobject NewDirectByteBuffer(JNIEnv *env, void *address, jlong capacity);
extern void *GetDirectBufferAddress(JNIEnv *env, jobject buf);

static void file_log(const char *msg) {
    SceUID fd = sceIoOpen(DATA_PATH "loader.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, msg, (SceSize)strlen(msg));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static int read_file(const char *path, unsigned char **out, size_t *out_len) {
    *out = NULL;
    *out_len = 0;
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    int sz = (int)sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (sz <= 0) {
        sceIoClose(fd);
        return 0;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) {
        sceIoClose(fd);
        return 0;
    }
    int n = sceIoRead(fd, buf, (SceSize)sz);
    sceIoClose(fd);
    if (n != sz) {
        free(buf);
        return 0;
    }
    *out = buf;
    *out_len = (size_t)sz;
    return 1;
}

static int resolve_asset(const char *name, unsigned char **out, size_t *out_len, char *used, size_t used_sz) {
    char candidates[5][512];
    int n = 0;
    if (strchr(name, ':')) {
        snprintf(candidates[n++], sizeof(candidates[0]), "%s", name);
    } else {
        snprintf(candidates[n++], sizeof(candidates[0]), DATA_PATH "assets/%s", name);
        snprintf(candidates[n++], sizeof(candidates[0]), DATA_PATH "%s", name);
        snprintf(candidates[n++], sizeof(candidates[0]), "ux0:/data/ctr/assets/%s", name);
        snprintf(candidates[n++], sizeof(candidates[0]), "ux0:/data/ctr/%s", name);
    }
    for (int i = 0; i < n; i++) {
        if (read_file(candidates[i], out, out_len)) {
            if (used && used_sz)
                snprintf(used, used_sz, "%s", candidates[i]);
            return 1;
        }
    }
    return 0;
}

/* Android Bitmap.getPixels: each int is 0xAARRGGBB in host endianness. */
static uint32_t *rgba_to_argb_ints(const unsigned char *rgba, int w, int h) {
    size_t n = (size_t)w * (size_t)h;
    uint32_t *argb = (uint32_t *)malloc(n * 4);
    if (!argb)
        return NULL;
    for (size_t i = 0; i < n; i++) {
        unsigned r = rgba[i * 4 + 0];
        unsigned g = rgba[i * 4 + 1];
        unsigned b = rgba[i * 4 + 2];
        unsigned a = rgba[i * 4 + 3];
        argb[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    return argb;
}

void ctr_load_image(const char *name, int tex_id) {
    if (!name || !name[0]) {
        file_log("loadImage: empty path");
        return;
    }

    unsigned char *file = NULL;
    size_t file_len = 0;
    char used[512];
    used[0] = 0;
    if (!resolve_asset(name, &file, &file_len, used, sizeof(used))) {
        char msg[640];
        snprintf(msg, sizeof(msg), "loadImage FAIL \"%s\"", name);
        file_log(msg);
        return;
    }

    int w = 0, h = 0, comp = 0;
    unsigned char *rgba = stbi_load_from_memory(file, (int)file_len, &w, &h, &comp, 4);
    free(file);
    if (!rgba || w <= 0 || h <= 0) {
        char msg[640];
        snprintf(msg, sizeof(msg), "loadImage decode FAIL \"%s\"", name);
        file_log(msg);
        if (rgba)
            stbi_image_free(rgba);
        return;
    }

    uint32_t *argb = rgba_to_argb_ints(rgba, w, h);
    stbi_image_free(rgba);
    if (!argb) {
        file_log("loadImage OOM");
        return;
    }

    jobject buf = NewDirectByteBuffer(&jni, argb, (jlong)((size_t)w * (size_t)h * 4));
    typedef void (*fn_imageLoaded)(JNIEnv *, jobject, jint, jobject, jint, jint);
    fn_imageLoaded imageLoaded =
        (fn_imageLoaded)so_symbol(&so_mod, "Java_com_zeptolab_ctr_CtrResourceLoader_imageLoaded");
    if (imageLoaded && buf) {
        char fake_thiz = 1;
        imageLoaded(&jni, (jobject)&fake_thiz, (jint)tex_id, buf, (jint)w, (jint)h);
    } else {
        file_log("loadImage: imageLoaded symbol or buffer missing");
    }

    /* imageLoaded copies into a POT texture; free our pixels + wrapper. */
    free(argb);
    free(buf);
}
