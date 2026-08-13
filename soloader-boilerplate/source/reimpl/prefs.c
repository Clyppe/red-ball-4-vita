/*
 * Stand-in for Android SharedPreferences / Cocos2dxHelper.getBoolForKey.
 * CCUserDefault::flush is a stub in this .so, so every set*ForKeyJNI must
 * persist here — but Game::save writes 75+ keys at once, so defer disk I/O
 * until prefs_flush() (once per frame).
 */

#include "reimpl/prefs.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREFS_PATH DATA_PATH "save/prefs.txt"
#define XML_PATH   DATA_PATH "save/UserDefault.xml"
#define MAX_KEYS   128
#define KEY_LEN    64
#define VAL_LEN    192

typedef struct {
    char key[KEY_LEN];
    char val[VAL_LEN];
} Pref;

static Pref g_prefs[MAX_KEYS];
static int g_n;
static int g_loaded;
static int g_dirty;

static void plog(const char *msg) {
    SceUID fd = sceIoOpen(DATA_PATH "loader.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, msg, (SceSize)strlen(msg));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static int find_key(const char *key) {
    for (int i = 0; i < g_n; i++) {
        if (strcmp(g_prefs[i].key, key) == 0)
            return i;
    }
    return -1;
}

static void set_raw(const char *key, const char *val) {
    if (!key || !val)
        return;
    int i = find_key(key);
    if (i < 0) {
        if (g_n >= MAX_KEYS)
            return;
        i = g_n++;
        snprintf(g_prefs[i].key, KEY_LEN, "%s", key);
    }
    snprintf(g_prefs[i].val, VAL_LEN, "%s", val);
    g_dirty = 1;
}

static void save_txt(void) {
    /* One contiguous write — hundreds of tiny sceIoWrite calls stall the Vita for seconds. */
    size_t cap = 256 + (size_t)g_n * (KEY_LEN + VAL_LEN + 4);
    char *buf = (char *)malloc(cap);
    if (!buf) {
        plog("prefs: alloc failed");
        return;
    }
    size_t used = 0;
    for (int i = 0; i < g_n; i++) {
        int n = snprintf(buf + used, cap - used, "%s=%s\n", g_prefs[i].key, g_prefs[i].val);
        if (n < 0 || (size_t)n >= cap - used)
            break;
        used += (size_t)n;
    }
    SceUID fd = sceIoOpen(PREFS_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) {
        plog("prefs: save txt failed");
        free(buf);
        return;
    }
    if (used)
        sceIoWrite(fd, buf, (SceSize)used);
    sceIoClose(fd);
    free(buf);
}

static void save_xml(void) {
    /* Cocos still fopen()s UserDefault.xml; keep a mirror so it exists. */
    size_t cap = 256 + (size_t)g_n * (KEY_LEN + VAL_LEN + 16);
    char *buf = (char *)malloc(cap);
    if (!buf)
        return;
    size_t used = 0;
    int n = snprintf(buf + used, cap - used,
                     "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<userDefaultRoot>\n");
    if (n > 0)
        used += (size_t)n;
    for (int i = 0; i < g_n; i++) {
        n = snprintf(buf + used, cap - used, "    <%s>%s</%s>\n",
                     g_prefs[i].key, g_prefs[i].val, g_prefs[i].key);
        if (n < 0 || (size_t)n >= cap - used)
            break;
        used += (size_t)n;
    }
    n = snprintf(buf + used, cap - used, "</userDefaultRoot>\n");
    if (n > 0)
        used += (size_t)n;

    SceUID fd = sceIoOpen(XML_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd >= 0) {
        if (used)
            sceIoWrite(fd, buf, (SceSize)used);
        sceIoClose(fd);
    }
    free(buf);
}

void prefs_flush(void) {
    if (!g_dirty)
        return;
    save_txt();
    /* XML only at prefs_init — rewriting it on every Game::save hitch the Vita. */
    g_dirty = 0;
}

static void load_txt(void) {
    SceUID fd = sceIoOpen(PREFS_PATH, SCE_O_RDONLY, 0);
    if (fd < 0)
        return;
    char buf[8192];
    int n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (n < 0)
        n = 0;
    buf[n] = 0;
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = 0;
        char *eq = strchr(line, '=');
        if (eq && eq != line) {
            *eq = 0;
            char *k = line;
            char *v = eq + 1;
            if (k[0] == '\r')
                k++;
            size_t lk = strlen(k);
            if (lk && k[lk - 1] == '\r')
                k[lk - 1] = 0;
            size_t lv = strlen(v);
            if (lv && v[lv - 1] == '\r')
                v[lv - 1] = 0;
            set_raw(k, v);
        }
        line = nl ? nl + 1 : NULL;
    }
}

void prefs_init(void) {
    if (g_loaded)
        return;
    g_n = 0;
    g_dirty = 0;
    load_txt();
    if (find_key("RedBall4_player_age_selected") < 0)
        set_raw("RedBall4_player_age_selected", "true");
    if (find_key("RedBall4_player_age") < 0)
        set_raw("RedBall4_player_age", "99");
    /* Enough lives that refill-video is rare, but not infinite IAP unlock. */
    if (find_key("lifes") < 0)
        set_raw("lifes", "5");
    if (find_key("lives") < 0)
        set_raw("lives", "5");
    prefs_flush();
    save_xml(); /* ensure UserDefault.xml exists for Cocos fopen() */
    g_loaded = 1;
    plog("prefs: init ok (age selected)");
}

int prefs_get_bool(const char *key, int def) {
    prefs_init();
    int i = find_key(key);
    if (i < 0)
        return def;
    const char *v = g_prefs[i].val;
    if (strcmp(v, "true") == 0 || strcmp(v, "1") == 0)
        return 1;
    if (strcmp(v, "false") == 0 || strcmp(v, "0") == 0)
        return 0;
    return def;
}

int prefs_get_int(const char *key, int def) {
    prefs_init();
    int i = find_key(key);
    if (i < 0)
        return def;
    return atoi(g_prefs[i].val);
}

float prefs_get_float(const char *key, float def) {
    prefs_init();
    int i = find_key(key);
    if (i < 0)
        return def;
    return (float)atof(g_prefs[i].val);
}

const char *prefs_get_string(const char *key, const char *def) {
    prefs_init();
    int i = find_key(key);
    if (i < 0)
        return def ? def : "";
    return g_prefs[i].val;
}

void prefs_set_bool(const char *key, int val) {
    prefs_init();
    set_raw(key, val ? "true" : "false");
}

void prefs_set_int(const char *key, int val) {
    prefs_init();
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", val);
    set_raw(key, buf);
}

void prefs_set_float(const char *key, float val) {
    prefs_init();
    char buf[32];
    snprintf(buf, sizeof(buf), "%g", val);
    set_raw(key, buf);
}

void prefs_set_string(const char *key, const char *val) {
    prefs_init();
    set_raw(key, val ? val : "");
}
