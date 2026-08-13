/*
 * Stand-in for Soomla NdkGlue.receiveCppMessage.
 * The .so talks to Java KeyValueStorage / virtual item storage over JSON.
 * Without this, CCStoreInfo dies with "store json is not in DB".
 */

#include "reimpl/soomla.h"

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

extern so_module so_mod;
extern jstring NewStringUTF(JNIEnv *env, const char *bytes);

#define KVS_DIR DATA_PATH "save/kvs/"
#define KVS_IDX DATA_PATH "save/kvs/_index.txt"
#define KVS_MAX 256
#define KEY_MAX 128

typedef struct {
    char key[KEY_MAX];
    char *val;
} KvsEnt;

static KvsEnt g_kvs[KVS_MAX];
static int g_n;
static int g_ready;
static int g_log_left = 6;

static void slog(const char *msg) {
    SceUID fd = sceIoOpen(DATA_PATH "loader.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, msg, (SceSize)strlen(msg));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static char *dupstr(const char *s) {
    if (!s)
        s = "";
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

static char *dupn(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p)
        return NULL;
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static void sanitize(const char *key, char *out, size_t n) {
    size_t j = 0;
    for (size_t i = 0; key[i] && j + 1 < n; i++) {
        char c = key[i];
        if (isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-')
            out[j++] = c;
        else
            out[j++] = '_';
    }
    out[j] = 0;
}

static int find_key(const char *key) {
    for (int i = 0; i < g_n; i++) {
        if (strcmp(g_kvs[i].key, key) == 0)
            return i;
    }
    return -1;
}

static void write_file(const char *path, const char *data, size_t len) {
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0)
        return;
    const char *p = data;
    size_t left = len;
    while (left) {
        int n = sceIoWrite(fd, p, left > 64 * 1024 ? 64 * 1024 : (SceSize)left);
        if (n <= 0)
            break;
        p += n;
        left -= (size_t)n;
    }
    sceIoClose(fd);
}

static char *read_file(const char *path, size_t *out_len) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return NULL;
    SceIoStat st;
    memset(&st, 0, sizeof(st));
    if (sceIoGetstat(path, &st) < 0) {
        sceIoClose(fd);
        return NULL;
    }
    size_t len = (size_t)st.st_size;
    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        sceIoClose(fd);
        return NULL;
    }
    size_t got = 0;
    while (got < len) {
        int n = sceIoRead(fd, buf + got, (SceSize)(len - got));
        if (n <= 0)
            break;
        got += (size_t)n;
    }
    sceIoClose(fd);
    buf[got] = 0;
    if (out_len)
        *out_len = got;
    return buf;
}

static void save_index(void) {
    SceUID fd = sceIoOpen(KVS_IDX, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0)
        return;
    for (int i = 0; i < g_n; i++) {
        char line[KEY_MAX + 2];
        int n = snprintf(line, sizeof(line), "%s\n", g_kvs[i].key);
        if (n > 0)
            sceIoWrite(fd, line, (SceSize)n);
    }
    sceIoClose(fd);
}

static void persist_one(int i) {
    char name[KEY_MAX + 8];
    char path[256];
    sanitize(g_kvs[i].key, name, sizeof(name));
    snprintf(path, sizeof(path), KVS_DIR "%s", name);
    const char *v = g_kvs[i].val ? g_kvs[i].val : "";
    write_file(path, v, strlen(v));
    save_index();
}

static void kvs_set(const char *key, const char *val) {
    if (!key)
        return;
    int i = find_key(key);
    if (i < 0) {
        if (g_n >= KVS_MAX)
            return;
        i = g_n++;
        snprintf(g_kvs[i].key, KEY_MAX, "%s", key);
        g_kvs[i].val = NULL;
    }
    free(g_kvs[i].val);
    g_kvs[i].val = dupstr(val ? val : "");
    persist_one(i);
}

static const char *kvs_get(const char *key) {
    int i = find_key(key);
    if (i < 0)
        return NULL;
    return g_kvs[i].val;
}

static void kvs_del(const char *key) {
    int i = find_key(key);
    if (i < 0)
        return;
    free(g_kvs[i].val);
    g_kvs[i] = g_kvs[g_n - 1];
    g_n--;
    save_index();
}

static void load_index(void) {
    char *idx = read_file(KVS_IDX, NULL);
    if (!idx)
        return;
    char *p = idx;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl)
            *nl = 0;
        size_t L = strlen(p);
        if (L && p[L - 1] == '\r')
            p[L - 1] = 0;
        if (*p) {
            char name[KEY_MAX + 8];
            char path[256];
            sanitize(p, name, sizeof(name));
            snprintf(path, sizeof(path), KVS_DIR "%s", name);
            char *val = read_file(path, NULL);
            if (g_n < KVS_MAX) {
                snprintf(g_kvs[g_n].key, KEY_MAX, "%s", p);
                g_kvs[g_n].val = val ? val : dupstr("");
                g_n++;
            } else {
                free(val);
            }
        }
        if (!nl)
            break;
        p = nl + 1;
    }
    free(idx);
}

static const char *skip_ws(const char *s) {
    while (*s && isspace((unsigned char)*s))
        s++;
    return s;
}

static const char *find_key_colon(const char *json, const char *key) {
    char pat[160];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = json;
    while ((p = strstr(p, pat)) != NULL) {
        const char *q = skip_ws(p + strlen(pat));
        if (*q == ':')
            return skip_ws(q + 1);
        p++;
    }
    return NULL;
}

static char *json_unescape_n(const char *s, size_t n) {
    char *out = (char *)malloc(n + 1);
    if (!out)
        return dupstr("");
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            char c = s[++i];
            switch (c) {
            case 'n': out[j++] = '\n'; break;
            case 'r': out[j++] = '\r'; break;
            case 't': out[j++] = '\t'; break;
            case '"': out[j++] = '"'; break;
            case '\\': out[j++] = '\\'; break;
            case '/': out[j++] = '/'; break;
            case 'u':
                out[j++] = '\\';
                out[j++] = 'u';
                break;
            default:
                out[j++] = c;
                break;
            }
        } else {
            out[j++] = s[i];
        }
    }
    out[j] = 0;
    return out;
}

static char *json_escape(const char *s) {
    if (!s)
        s = "";
    size_t n = strlen(s);
    char *out = (char *)malloc(n * 2 + 1);
    if (!out)
        return dupstr("");
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': out[j++] = '\\'; out[j++] = '"'; break;
        case '\\': out[j++] = '\\'; out[j++] = '\\'; break;
        case '\n': out[j++] = '\\'; out[j++] = 'n'; break;
        case '\r': out[j++] = '\\'; out[j++] = 'r'; break;
        case '\t': out[j++] = '\\'; out[j++] = 't'; break;
        default:
            out[j++] = (char)c;
            break;
        }
    }
    out[j] = 0;
    return out;
}

static char *json_get_string(const char *json, const char *key) {
    const char *p = find_key_colon(json, key);
    if (!p || *p != '"')
        return NULL;
    p++;
    const char *start = p;
    while (*p) {
        if (*p == '\\' && p[1]) {
            p += 2;
            continue;
        }
        if (*p == '"')
            break;
        p++;
    }
    return json_unescape_n(start, (size_t)(p - start));
}

static int json_get_int(const char *json, const char *key, int def) {
    const char *p = find_key_colon(json, key);
    if (!p)
        return def;
    if (*p == '"')
        return atoi(p + 1);
    return atoi(p);
}

static char *json_get_object(const char *json, const char *key) {
    const char *p = find_key_colon(json, key);
    if (!p || *p != '{')
        return NULL;
    int depth = 0;
    int in_str = 0;
    const char *start = p;
    for (; *p; p++) {
        if (in_str) {
            if (*p == '\\' && p[1]) {
                p++;
                continue;
            }
            if (*p == '"')
                in_str = 0;
            continue;
        }
        if (*p == '"') {
            in_str = 1;
            continue;
        }
        if (*p == '{')
            depth++;
        else if (*p == '}') {
            depth--;
            if (depth == 0)
                return dupn(start, (size_t)(p - start + 1));
        }
    }
    return NULL;
}

static char *ret_empty(void) {
    return dupstr("{}");
}

static char *ret_int(int v) {
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"return\":%d}", v);
    return dupstr(buf);
}

static char *ret_bool(int v) {
    return dupstr(v ? "{\"return\":true}" : "{\"return\":false}");
}

static char *ret_str(const char *s) {
    char *esc = json_escape(s ? s : "");
    size_t n = strlen(esc) + 16;
    char *out = (char *)malloc(n);
    if (!out) {
        free(esc);
        return ret_empty();
    }
    snprintf(out, n, "{\"return\":\"%s\"}", esc);
    free(esc);
    return out;
}

static int default_balance(const char *itemId) {
    if (!itemId)
        return 0;
    if (strstr(itemId, "life") || strstr(itemId, "Life") || strstr(itemId, "LIVE"))
        return 99;
    if (strstr(itemId, "gold") || strstr(itemId, "Gold") || strstr(itemId, "coin") || strstr(itemId, "Coin"))
        return 9999;
    if (strstr(itemId, "unlock") || strstr(itemId, "removeads") || strstr(itemId, "premium") ||
        strstr(itemId, "noads") || strstr(itemId, "shirt"))
        return 1;
    return 0;
}

static const char *bal_key(const char *method, const char *itemId, char *buf, size_t n) {
    const char *kind = strstr(method, "VirtualGoods") ? "good" : "currency";
    snprintf(buf, n, "%s.%s.balance", kind, itemId ? itemId : "");
    return buf;
}

static int get_balance(const char *method, const char *itemId) {
    char key[KEY_MAX];
    bal_key(method, itemId, key, sizeof(key));
    const char *v = kvs_get(key);
    if (v)
        return atoi(v);
    int d = default_balance(itemId);
    char num[16];
    snprintf(num, sizeof(num), "%d", d);
    kvs_set(key, num);
    return d;
}

static int set_balance(const char *method, const char *itemId, int bal) {
    if (bal < 0)
        bal = 0;
    char key[KEY_MAX];
    char num[16];
    bal_key(method, itemId, key, sizeof(key));
    snprintf(num, sizeof(num), "%d", bal);
    kvs_set(key, num);
    return bal;
}

static void send_cpp(const char *json) {
    typedef void (*fn_send)(JNIEnv *, jobject, jstring);
    static fn_send send;
    if (!send)
        send = (fn_send)so_symbol(&so_mod, "Java_com_soomla_cocos2dx_common_NdkGlue_sendCppMessage");
    if (!send) {
        slog("soomla: sendCppMessage symbol missing");
        return;
    }
    send(&jni, NULL, NewStringUTF(&jni, json));
}

static void seed_store_aliases(const char *store_json) {
    kvs_set("meta.storeinfo", store_json);
    kvs_set("storeInfo", store_json);
    kvs_set("soomla.store.storeInfo", store_json);
}

void soomla_init(void) {
    if (g_ready)
        return;
    sceIoMkdir(DATA_PATH "save", 0777);
    sceIoMkdir(KVS_DIR, 0777);
    load_index();
    g_ready = 1;
    char line[64];
    snprintf(line, sizeof(line), "soomla: init keys=%d", g_n);
    slog(line);
}

char *soomla_receive(const char *json) {
    soomla_init();
    if (!json)
        json = "{}";

    char *method = json_get_string(json, "method");
    if (!method)
        method = dupstr("");

    if (g_log_left > 0) {
        g_log_left--;
        char line[280];
        snprintf(line, sizeof(line), "soomla: %s len=%d", method, (int)strlen(json));
        slog(line);
    }

    char *out = NULL;

    if (strcmp(method, "CCNativeKeyValueStorage::getValue") == 0) {
        char *key = json_get_string(json, "key");
        const char *v = key ? kvs_get(key) : NULL;
        out = v ? ret_str(v) : ret_empty();
        free(key);
    } else if (strcmp(method, "CCNativeKeyValueStorage::setValue") == 0) {
        char *key = json_get_string(json, "key");
        char *val = json_get_string(json, "val");
        if (!val)
            val = json_get_string(json, "value");
        if (key)
            kvs_set(key, val ? val : "");
        out = ret_empty();
        free(key);
        free(val);
    } else if (strcmp(method, "CCNativeKeyValueStorage::deleteKeyValue") == 0) {
        char *key = json_get_string(json, "key");
        if (key)
            kvs_del(key);
        out = ret_empty();
        free(key);
    } else if (strcmp(method, "CCNativeKeyValueStorage::purge") == 0) {
        for (int i = 0; i < g_n; i++)
            free(g_kvs[i].val);
        g_n = 0;
        save_index();
        out = ret_empty();
    } else if (strcmp(method, "CCNativeKeyValueStorage::getEncryptedKeys") == 0) {
        size_t cap = 32;
        for (int i = 0; i < g_n; i++)
            cap += strlen(g_kvs[i].key) * 2 + 8;
        char *buf = (char *)malloc(cap);
        if (!buf) {
            out = dupstr("{\"return\":[]}");
        } else {
            strcpy(buf, "{\"return\":[");
            for (int i = 0; i < g_n; i++) {
                char *esc = json_escape(g_kvs[i].key);
                if (i)
                    strcat(buf, ",");
                strcat(buf, "\"");
                strcat(buf, esc);
                strcat(buf, "\"");
                free(esc);
            }
            strcat(buf, "]}");
            out = buf;
        }
    } else if (strcmp(method, "CCStoreAssets::init") == 0) {
        char *assets = json_get_object(json, "storeAssets");
        if (assets) {
            char line[80];
            snprintf(line, sizeof(line), "soomla: storeAssets %d bytes", (int)strlen(assets));
            slog(line);
            seed_store_aliases(assets);
            free(assets);
        } else {
            slog("soomla: CCStoreAssets::init missing storeAssets");
        }
        out = ret_empty();
    } else if (strcmp(method, "CCStoreInfo::loadFromDB") == 0 ||
               strcmp(method, "CCNativeStoreInfo::save") == 0) {
        if (strcmp(method, "CCStoreInfo::loadFromDB") == 0)
            send_cpp("{\"method\":\"Reflection::CCStoreInfo::initializeFromDB\"}");
        out = ret_empty();
    } else if (strstr(method, "getBalance")) {
        char *item = json_get_string(json, "itemId");
        out = ret_int(get_balance(method, item));
        free(item);
    } else if (strstr(method, "setBalance")) {
        char *item = json_get_string(json, "itemId");
        int bal = json_get_int(json, "balance", default_balance(item));
        out = ret_int(set_balance(method, item, bal));
        free(item);
    } else if (strstr(method, "::add")) {
        char *item = json_get_string(json, "itemId");
        int amt = json_get_int(json, "amount", 0);
        int bal = get_balance(method, item) + amt;
        out = ret_int(set_balance(method, item, bal));
        free(item);
    } else if (strstr(method, "::remove") && !strstr(method, "removeUpgrades") &&
               !strstr(method, "deleteKey")) {
        char *item = json_get_string(json, "itemId");
        int amt = json_get_int(json, "amount", 0);
        int bal = get_balance(method, item) - amt;
        out = ret_int(set_balance(method, item, bal));
        free(item);
    } else if (strstr(method, "isEquipped")) {
        char *item = json_get_string(json, "itemId");
        char key[KEY_MAX];
        snprintf(key, sizeof(key), "good.%s.equipped", item ? item : "");
        const char *v = kvs_get(key);
        out = ret_bool(v && atoi(v));
        free(item);
    } else if (strstr(method, "::equip") && !strstr(method, "unEquip") && !strstr(method, "unequip")) {
        char *item = json_get_string(json, "itemId");
        char key[KEY_MAX];
        snprintf(key, sizeof(key), "good.%s.equipped", item ? item : "");
        kvs_set(key, "1");
        out = ret_empty();
        free(item);
    } else if (strstr(method, "unequip") || strstr(method, "unEquip")) {
        char *item = json_get_string(json, "itemId");
        char key[KEY_MAX];
        snprintf(key, sizeof(key), "good.%s.equipped", item ? item : "");
        kvs_set(key, "0");
        out = ret_empty();
        free(item);
    } else if (strstr(method, "getCurrentUpgrade")) {
        out = ret_empty();
    } else if (strstr(method, "getTimesGiven") || strstr(method, "getLastSeqIdxGiven") ||
               strstr(method, "getGoodUpgradeLevel")) {
        out = ret_int(0);
    } else if (strstr(method, "canAfford")) {
        out = ret_bool(1);
    } else {
        /* initialize, billing, IAP, rewards, refresh — succeed with empty object */
        out = ret_empty();
    }

    free(method);
    return out ? out : ret_empty();
}
