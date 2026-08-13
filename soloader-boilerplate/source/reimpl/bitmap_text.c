/*
 * Cocos2d-x 2.x paints CCLabelTTF via
 * Cocos2dxBitmap.createTextBitmapShadowStroke → nativeInitBitmapDC.
 * Rasterize with stb_truetype using the game's bundled TTF files.
 *
 * CCImage marks these bitmaps as premultiplied-alpha and Cocos blends with
 * GL_ONE / GL_ONE_MINUS_SRC_ALPHA. Writing straight white RGB + coverage
 * alpha produces a pixelated white halo behind every glyph — RGB must be
 * multiplied by alpha before nativeInitBitmapDC.
 */

#include "reimpl/bitmap_text.h"

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include <psp2/io/fcntl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb/stb_truetype.h>

extern so_module so_mod;
extern const char *GetStringUTFChars(JNIEnv *env, jstring string, jboolean *isCopy);
extern void ReleaseStringUTFChars(JNIEnv *env, jstring string, char *utf);
extern jbyteArray NewByteArray(JNIEnv *env, jsize length);
extern void SetByteArrayRegion(JNIEnv *env, jbyteArray array, jsize start, jsize len, const jbyte *buf);

typedef void (*fn_init_bmp)(JNIEnv *env, jobject thiz, jint width, jint height, jbyteArray pixels);

static char fake_bitmap_thiz = 42;

static unsigned char *g_ttf_data;
static size_t g_ttf_size;
static stbtt_fontinfo g_font;
static int g_font_ready;
static char g_font_loaded[96];
static int g_logged_init;
static int g_log_errors_left = 8;

#define SS 2

static void blog(const char *msg) {
    SceUID fd = sceIoOpen(DATA_PATH "loader.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd < 0)
        return;
    sceIoWrite(fd, msg, (SceSize)strlen(msg));
    sceIoWrite(fd, "\n", 1);
    sceIoClose(fd);
}

static unsigned char *read_file(const char *path, size_t *out_size) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return NULL;
    long long end = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (end < 64 || end > 8 * 1024 * 1024) {
        sceIoClose(fd);
        return NULL;
    }
    size_t n = (size_t)end;
    unsigned char *buf = (unsigned char *)malloc(n);
    if (!buf) {
        sceIoClose(fd);
        return NULL;
    }
    int got = sceIoRead(fd, buf, (SceSize)n);
    sceIoClose(fd);
    if (got != (int)n) {
        free(buf);
        return NULL;
    }
    *out_size = n;
    return buf;
}

static int try_load_font(const char *path) {
    size_t n = 0;
    unsigned char *data = read_file(path, &n);
    if (!data)
        return 0;
    if (!stbtt_InitFont(&g_font, data, stbtt_GetFontOffsetForIndex(data, 0))) {
        free(data);
        return 0;
    }
    free(g_ttf_data);
    g_ttf_data = data;
    g_ttf_size = n;
    g_font_ready = 1;
    strncpy(g_font_loaded, path, sizeof(g_font_loaded) - 1);
    g_font_loaded[sizeof(g_font_loaded) - 1] = 0;
    if (!g_logged_init) {
        char line[192];
        snprintf(line, sizeof(line), "ttf: loaded %s (%u bytes)", path, (unsigned)n);
        blog(line);
        g_logged_init = 1;
    }
    return 1;
}

static void ensure_font(const char *font_name) {
    const char *prefer_shark = DATA_PATH "assets/Fonts/TTF/SharkSoftBites.ttf";
    const char *prefer_robo = DATA_PATH "assets/Fonts/TTF/Roboto-Black.ttf";

    int want_robo = 0;
    if (font_name) {
        if (strstr(font_name, "Roboto") || strstr(font_name, "roboto") ||
            strstr(font_name, "sans") || strstr(font_name, "Sans") ||
            strstr(font_name, "Default") || strstr(font_name, "default"))
            want_robo = 1;
    }

    const char *primary = want_robo ? prefer_robo : prefer_shark;
    const char *secondary = want_robo ? prefer_shark : prefer_robo;

    if (g_font_ready && strcmp(g_font_loaded, primary) == 0)
        return;

    if (try_load_font(primary))
        return;
    if (g_font_ready && strcmp(g_font_loaded, secondary) == 0)
        return;
    if (try_load_font(secondary))
        return;

    if (g_log_errors_left > 0) {
        g_log_errors_left--;
        blog("ttf: failed to load game TTF");
    }
}

static int next_utf8(const char *s, int *i, int len) {
    if (*i >= len)
        return 0;
    unsigned char c = (unsigned char)s[*i];
    if (c < 0x80) {
        (*i)++;
        return (int)c;
    }
    if ((c & 0xE0) == 0xC0 && *i + 1 < len) {
        int cp = ((c & 0x1F) << 6) | ((unsigned char)s[*i + 1] & 0x3F);
        *i += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && *i + 2 < len) {
        int cp = ((c & 0x0F) << 12) |
                 (((unsigned char)s[*i + 1] & 0x3F) << 6) |
                 ((unsigned char)s[*i + 2] & 0x3F);
        *i += 3;
        return cp;
    }
    (*i)++;
    return (int)c;
}

typedef struct {
    int cp;
    int x0, y0, x1, y1;
    int advance;
    int x_off;
} GlyphMeas;

#define MAX_GLYPHS 128
#define MAX_LINES 8

static void blit_coverage(uint8_t *rgba, int width, int height,
                          int dx, int dy, int gw, int gh,
                          const unsigned char *src, int src_stride,
                          uint8_t cr, uint8_t cg, uint8_t cb) {
    for (int y = 0; y < gh; y++) {
        int py = dy + y;
        if (py < 0 || py >= height)
            continue;
        for (int x = 0; x < gw; x++) {
            int px = dx + x;
            if (px < 0 || px >= width)
                continue;
            unsigned char a = src[y * src_stride + x];
            if (!a)
                continue;
            size_t o = ((size_t)py * (size_t)width + (size_t)px) * 4u;
            unsigned char da = rgba[o + 3];
            if (a < da)
                continue;
            /* Premultiplied RGBA — required by CCImage/CCTexture2D blend. */
            rgba[o + 0] = (uint8_t)((cr * a) / 255);
            rgba[o + 1] = (uint8_t)((cg * a) / 255);
            rgba[o + 2] = (uint8_t)((cb * a) / 255);
            rgba[o + 3] = a;
        }
    }
}

static void downsample_2x(const unsigned char *hi, int hw, int hh,
                          unsigned char *lo, int lw, int lh) {
    for (int y = 0; y < lh; y++) {
        for (int x = 0; x < lw; x++) {
            int sx = x * SS;
            int sy = y * SS;
            unsigned int sum = 0;
            int n = 0;
            for (int oy = 0; oy < SS; oy++) {
                int yy = sy + oy;
                if (yy >= hh)
                    continue;
                for (int ox = 0; ox < SS; ox++) {
                    int xx = sx + ox;
                    if (xx >= hw)
                        continue;
                    sum += hi[yy * hw + xx];
                    n++;
                }
            }
            lo[y * lw + x] = n ? (unsigned char)((sum + n / 2) / n) : 0;
        }
    }
}

void java_createTextBitmapShadowStroke(jmethodID id, va_list args) {
    (void)id;

    jobject text_obj = va_arg(args, jobject);
    jobject font_obj = va_arg(args, jobject);

    jint fontSize = va_arg(args, jint);
    double tintR = va_arg(args, double);
    double tintG = va_arg(args, double);
    double tintB = va_arg(args, double);

    const char *text = "";
    int need_release_text = 0;
    if (text_obj) {
        const char *got = GetStringUTFChars(&jni, text_obj, NULL);
        if (got) {
            text = got;
            need_release_text = 1;
        }
    }

    const char *font_name = NULL;
    int need_release_font = 0;
    if (font_obj) {
        const char *got = GetStringUTFChars(&jni, font_obj, NULL);
        if (got) {
            font_name = got;
            need_release_font = 1;
        }
    }

    ensure_font(font_name);

    if (fontSize < 8 || fontSize > 160)
        fontSize = 32;

    float pixel_h = (float)fontSize * 0.92f;

    uint8_t cr = 255, cg = 255, cb = 255;
    if (tintR >= 0.0 && tintR <= 1.0 && tintG >= 0.0 && tintG <= 1.0 && tintB >= 0.0 && tintB <= 1.0) {
        cr = (uint8_t)(tintR * 255.0 + 0.5);
        cg = (uint8_t)(tintG * 255.0 + 0.5);
        cb = (uint8_t)(tintB * 255.0 + 0.5);
    }

    if (!g_font_ready) {
        uint8_t px[4] = {0, 0, 0, 0};
        static fn_init_bmp nativeInitBitmapDC;
        if (!nativeInitBitmapDC)
            nativeInitBitmapDC = (fn_init_bmp)so_symbol(
                &so_mod, "Java_org_cocos2dx_lib_Cocos2dxBitmap_nativeInitBitmapDC");
        if (nativeInitBitmapDC) {
            jbyteArray arr = NewByteArray(&jni, 4);
            if (arr) {
                SetByteArrayRegion(&jni, arr, 0, 4, (const jbyte *)px);
                nativeInitBitmapDC(&jni, (jobject)&fake_bitmap_thiz, 1, 1, arr);
            }
        }
        if (need_release_text)
            ReleaseStringUTFChars(&jni, text_obj, (char *)text);
        if (need_release_font)
            ReleaseStringUTFChars(&jni, font_obj, (char *)font_name);
        return;
    }

    float scale = stbtt_ScaleForPixelHeight(&g_font, pixel_h);
    float scale_hi = scale * (float)SS;
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &lineGap);
    int baseline = (int)(ascent * scale + 0.5f);
    int line_h = (int)((ascent - descent + lineGap) * scale + 0.5f);
    if (line_h < (int)(pixel_h + 0.5f))
        line_h = (int)(pixel_h + 0.5f);

    GlyphMeas glyphs[MAX_GLYPHS];
    int n_glyphs = 0;
    int line_w[MAX_LINES];
    int line_start[MAX_LINES];
    int n_lines = 1;
    line_start[0] = 0;
    line_w[0] = 0;

    int text_len = (int)strlen(text);
    int i = 0;
    int pen_x = 0;
    int max_w = 0;

    while (i < text_len && n_glyphs < MAX_GLYPHS) {
        int cp = next_utf8(text, &i, text_len);
        if (cp == '\r')
            continue;
        if (cp == '\n') {
            if (pen_x > max_w)
                max_w = pen_x;
            line_w[n_lines - 1] = pen_x;
            if (n_lines >= MAX_LINES)
                break;
            line_start[n_lines] = n_glyphs;
            line_w[n_lines] = 0;
            n_lines++;
            pen_x = 0;
            continue;
        }

        int ax, lsb;
        stbtt_GetCodepointHMetrics(&g_font, cp, &ax, &lsb);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&g_font, cp, scale, scale, &x0, &y0, &x1, &y1);

        GlyphMeas *g = &glyphs[n_glyphs++];
        g->cp = cp;
        g->x0 = x0;
        g->y0 = y0;
        g->x1 = x1;
        g->y1 = y1;
        g->advance = (int)(ax * scale + 0.5f);
        g->x_off = pen_x;
        (void)lsb;
        pen_x += g->advance;
        if (pen_x <= 0 && (x1 - x0) > 0)
            pen_x = x1 - x0;
    }
    line_w[n_lines - 1] = pen_x;
    if (pen_x > max_w)
        max_w = pen_x;

    if (n_glyphs < 1) {
        glyphs[0].cp = ' ';
        glyphs[0].x0 = 0;
        glyphs[0].y0 = 0;
        glyphs[0].x1 = fontSize / 3;
        glyphs[0].y1 = 0;
        glyphs[0].advance = fontSize / 3;
        glyphs[0].x_off = 0;
        n_glyphs = 1;
        n_lines = 1;
        line_start[0] = 0;
        line_w[0] = glyphs[0].advance;
        max_w = line_w[0];
    }

    int pad = 2;
    int width = max_w + pad * 2;
    int height = n_lines * line_h + pad * 2;
    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;
    if (width > 1024)
        width = 1024;
    if (height > 512)
        height = 512;

    size_t nbytes = (size_t)width * (size_t)height * 4u;
    uint8_t *rgba = (uint8_t *)calloc(1, nbytes);
    if (!rgba) {
        if (g_log_errors_left > 0) {
            g_log_errors_left--;
            blog("ttf: alloc failed");
        }
        if (need_release_text)
            ReleaseStringUTFChars(&jni, text_obj, (char *)text);
        if (need_release_font)
            ReleaseStringUTFChars(&jni, font_obj, (char *)font_name);
        return;
    }

    int gi = 0;
    for (int li = 0; li < n_lines; li++) {
        int end = (li + 1 < n_lines) ? line_start[li + 1] : n_glyphs;
        int y_base = pad + li * line_h + baseline;
        for (; gi < end; gi++) {
            GlyphMeas *g = &glyphs[gi];
            int gw = g->x1 - g->x0;
            int gh = g->y1 - g->y0;
            if (gw <= 0 || gh <= 0)
                continue;

            int dx = pad + g->x_off + g->x0;
            int dy = y_base + g->y0;
            if (dx >= width || dy >= height)
                continue;

            int x0h, y0h, x1h, y1h;
            stbtt_GetCodepointBitmapBox(&g_font, g->cp, scale_hi, scale_hi, &x0h, &y0h, &x1h, &y1h);
            int gwh = x1h - x0h;
            int ghh = y1h - y0h;
            if (gwh <= 0 || ghh <= 0)
                continue;

            unsigned char *hi = (unsigned char *)malloc((size_t)gwh * (size_t)ghh);
            if (!hi)
                continue;
            stbtt_MakeCodepointBitmap(&g_font, hi, gwh, ghh, gwh, scale_hi, scale_hi, g->cp);

            unsigned char *lo = (unsigned char *)malloc((size_t)gw * (size_t)gh);
            if (!lo) {
                free(hi);
                continue;
            }
            downsample_2x(hi, gwh, ghh, lo, gw, gh);
            free(hi);

            blit_coverage(rgba, width, height, dx, dy, gw, gh, lo, gw, cr, cg, cb);
            free(lo);
        }
    }

    static fn_init_bmp nativeInitBitmapDC;
    if (!nativeInitBitmapDC) {
        nativeInitBitmapDC = (fn_init_bmp)so_symbol(
            &so_mod, "Java_org_cocos2dx_lib_Cocos2dxBitmap_nativeInitBitmapDC");
    }
    if (!nativeInitBitmapDC) {
        if (g_log_errors_left > 0) {
            g_log_errors_left--;
            blog("ttf: nativeInitBitmapDC missing");
        }
        free(rgba);
        if (need_release_text)
            ReleaseStringUTFChars(&jni, text_obj, (char *)text);
        if (need_release_font)
            ReleaseStringUTFChars(&jni, font_obj, (char *)font_name);
        return;
    }

    jbyteArray arr = NewByteArray(&jni, (jsize)nbytes);
    if (!arr) {
        if (g_log_errors_left > 0) {
            g_log_errors_left--;
            blog("ttf: NewByteArray failed");
        }
        free(rgba);
        if (need_release_text)
            ReleaseStringUTFChars(&jni, text_obj, (char *)text);
        if (need_release_font)
            ReleaseStringUTFChars(&jni, font_obj, (char *)font_name);
        return;
    }
    SetByteArrayRegion(&jni, arr, 0, (jsize)nbytes, (const jbyte *)rgba);
    nativeInitBitmapDC(&jni, (jobject)&fake_bitmap_thiz, width, height, arr);

    free(rgba);
    if (need_release_text)
        ReleaseStringUTFChars(&jni, text_obj, (char *)text);
    if (need_release_font)
        ReleaseStringUTFChars(&jni, font_obj, (char *)font_name);
}
