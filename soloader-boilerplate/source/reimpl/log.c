/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/log.h"
#include "reimpl/sys.h"
#include "utils/logger.h"
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <stdlib.h>
#include <string.h>

#define print_common \
    switch (prio) { \
        case ANDROID_LOG_INFO: \
            l_info("[ALOG][%s] %s", tag, text); \
            break; \
        case ANDROID_LOG_WARN: \
            l_warn("[ALOG][%s] %s", tag, text); \
            break; \
        case ANDROID_LOG_ERROR: \
        case ANDROID_LOG_FATAL: \
            l_error("[ALOG][%s] %s", tag, text); \
            break; \
        case ANDROID_LOG_UNKNOWN: \
        case ANDROID_LOG_DEFAULT: \
        case ANDROID_LOG_VERBOSE: \
        case ANDROID_LOG_DEBUG: \
        case ANDROID_LOG_SILENT: \
        default: \
            l_debug("[ALOG][%s] %s", tag, text); \
            break; \
    }

int __android_log_write(int prio, const char* tag, const char* text) {
    print_common
    return 0;
}

int __android_log_print(int prio, const char* tag, const char* fmt, ...) {
    va_list list;
    char text[1024];

    va_start(list, fmt);
    sceClibVsnprintf(text, sizeof(text), fmt, list);
    va_end(list);

    print_common

    return 0;
}

int __android_log_vprint(int prio, const char* tag, const char* fmt, va_list ap) {
    char text[1024];

    sceClibVsnprintf(text, sizeof(text), fmt, ap);

    print_common

    return 0;
}

void __android_log_assert(const char* cond, const char* tag, const char* fmt, ...) {
    char text[1024];
    text[0] = 0;
    if (fmt) {
        va_list list;
        va_start(list, fmt);
        sceClibVsnprintf(text, sizeof(text), fmt, list);
        va_end(list);
        l_fatal("[ALOG][ASSERT][%s] %s", tag ? tag : "?", text);
    } else if (cond) {
        sceClibSnprintf(text, sizeof(text), "Assertion failed: %s", cond);
        l_fatal("[ALOG][ASSERT][%s] %s", tag ? tag : "?", text);
    } else {
        sceClibSnprintf(text, sizeof(text), "Unspecified assertion failed");
        l_fatal("[ALOG][ASSERT][%s] %s", tag ? tag : "?", text);
    }

    SceUID fd = sceIoOpen("ux0:data/ctr/loader.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, "FATAL: assert: ", 15);
        sceIoWrite(fd, text, (SceSize)strlen(text));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }

    abort_soloader();
}
