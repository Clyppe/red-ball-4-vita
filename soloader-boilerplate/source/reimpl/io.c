/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/io.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/logger.h"
#include "utils/utils.h"

// Includes the following inline utilities:
// int oflags_musl_to_newlib(int flags);
// dirent64_bionic * dirent_newlib_to_bionic(struct dirent* dirent_newlib);
// void stat_newlib_to_bionic(struct stat * src, stat64_bionic * dst);
#include "reimpl/bits/_struct_converters.c"

static void io_file_log(const char *msg) {
    SceUID fd = sceIoOpen(DATA_PATH "loader.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, msg, (SceSize)strlen(msg));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static FILE *fopen_try(const char *path, const char *mode) {
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fopen(path, mode);
#else
    return fopen(path, mode);
#endif
}

static int remap_android_path(const char *in, char *out, size_t n) {
    static const char *pfx[] = {
        "/data/data/com.FDGEntertainment.redball4.gp/files/",
        "/data/data/com.FDGEntertainment.redball4.gp/",
        "/data/user/0/com.FDGEntertainment.redball4.gp/files/",
        "/data/user/0/com.FDGEntertainment.redball4.gp/",
    };
    for (int i = 0; i < 4; i++) {
        size_t L = strlen(pfx[i]);
        if (strncmp(in, pfx[i], L) == 0) {
            const char *rest = in + L;
            while (*rest == '/')
                rest++;
            if (*rest)
                snprintf(out, n, DATA_PATH "save/%s", rest);
            else
                snprintf(out, n, DATA_PATH "save/");
            return 1;
        }
    }
    return 0;
}

static void fopen_fail_log(const char *filename, const char *mode) {
    static int nfail;
    if (nfail++ >= 12)
        return;
    char msg[640];
    if (nfail == 12)
        snprintf(msg, sizeof(msg), "fopen FAIL (suppressing further)");
    else
        snprintf(msg, sizeof(msg), "fopen FAIL \"%s\" mode=%s", filename, mode ? mode : "?");
    io_file_log(msg);
}

FILE * fopen_soloader(const char * filename, const char * mode) {
    if (!filename)
        return NULL;
    if (strcmp(filename, "/proc/cpuinfo") == 0) {
        return fopen_soloader("app0:/cpuinfo", mode);
    } else if (strcmp(filename, "/proc/meminfo") == 0) {
        return fopen_soloader("app0:/meminfo", mode);
    }

    char remapped[512];
    if (remap_android_path(filename, remapped, sizeof(remapped)))
        filename = remapped;

    FILE *ret = fopen_try(filename, mode);
    if (ret)
        return ret;

    /* Relative asset paths from classic CTR — resolve under ux0:data/ctr/ */
    if (!strchr(filename, ':')) {
        char a[512], b[512], c[512], d[512];
        const char *cands[4];
        int n = 0;
        snprintf(a, sizeof(a), DATA_PATH "assets/%s", filename);
        snprintf(b, sizeof(b), DATA_PATH "%s", filename);
        snprintf(c, sizeof(c), "ux0:/data/ctr/assets/%s", filename);
        snprintf(d, sizeof(d), "ux0:/data/ctr/%s", filename);
        cands[n++] = a;
        cands[n++] = b;
        cands[n++] = c;
        cands[n++] = d;
        for (int i = 0; i < n; i++) {
            ret = fopen_try(cands[i], mode);
            if (ret)
                return ret;
        }
    }

    fopen_fail_log(filename, mode);
    return NULL;
}

int open_soloader(const char * path, int oflag, ...) {
    if (!path)
        return -1;
    if (strcmp(path, "/proc/cpuinfo") == 0) {
        return open_soloader("app0:/cpuinfo", oflag);
    } else if (strcmp(path, "/proc/meminfo") == 0) {
        return open_soloader("app0:/meminfo", oflag);
    } else if (strcmp(path, "/dev/urandom") == 0) {
        return open_soloader("app0:/urandom", oflag);
    }

    mode_t mode = 0666;
    if (((oflag & BIONIC_O_CREAT) == BIONIC_O_CREAT) ||
        ((oflag & BIONIC_O_TMPFILE) == BIONIC_O_TMPFILE)) {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)(va_arg(args, int));
        va_end(args);
    }

    oflag = oflags_bionic_to_newlib(oflag);

    char remapped[512];
    if (remap_android_path(path, remapped, sizeof(remapped)))
        path = remapped;

    int ret = open(path, oflag, mode);
    if (ret >= 0)
        return ret;

    if (!strchr(path, ':')) {
        char a[512], b[512];
        snprintf(a, sizeof(a), DATA_PATH "assets/%s", path);
        ret = open(a, oflag, mode);
        if (ret >= 0)
            return ret;
        snprintf(b, sizeof(b), DATA_PATH "%s", path);
        ret = open(b, oflag, mode);
        if (ret >= 0)
            return ret;
    }

    char msg[640];
    snprintf(msg, sizeof(msg), "open FAIL \"%s\"", path);
    io_file_log(msg);
    return -1;
}

int fstat_soloader(int fd, stat64_bionic * buf) {
    struct stat st;
    int res = fstat(fd, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("fstat(%i): %i", fd, res);
    return res;
}

int stat_soloader(const char * path, stat64_bionic * buf) {
    if (strcmp(path, "/system/lib/libOpenSLES.so") == 0) {
        l_debug("stat(%s): returning 0 in case this is a check for OpenSLES support", path);
        return 0;
    }

    struct stat st;
    int res = stat(path, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("stat(%s): %i", path, res);
    return res;
}

int fclose_soloader(FILE * f) {
#ifdef USE_SCELIBC_IO
    int ret = sceLibcBridge_fclose(f);
#else
    int ret = fclose(f);
#endif

    l_debug("fclose(%p): %i", f, ret);
    return ret;
}

int close_soloader(int fd) {
    int ret = close(fd);
    l_debug("close(%i): %i", fd, ret);
    return ret;
}

DIR* opendir_soloader(char* _pathname) {
    DIR* ret = opendir(_pathname);
    l_debug("opendir(\"%s\"): %p", _pathname, ret);
    return ret;
}

struct dirent64_bionic * readdir_soloader(DIR * dir) {
    static struct dirent64_bionic dirent_tmp;

    struct dirent* ret = readdir(dir);
    l_debug("readdir(%p): %p", dir, ret);

    if (ret) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(ret);
        memcpy(&dirent_tmp, entry_tmp, sizeof(dirent64_bionic));
        free(entry_tmp);
        return &dirent_tmp;
    }

    return NULL;
}

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result) {
    struct dirent dirent_tmp;
    struct dirent * pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(&dirent_tmp);
        memcpy(entry, entry_tmp, sizeof(dirent64_bionic));
        *result = (pdirent_tmp != NULL) ? entry : NULL;
        free(entry_tmp);
    }

    l_debug("readdir_r(%p, %p, %p): %i", dirp, entry, result, ret);
    return ret;
}

int closedir_soloader(DIR * dir) {
    int ret = closedir(dir);
    l_debug("closedir(%p): %i", dir, ret);
    return ret;
}

int fcntl_soloader(int fd, int cmd, ...) {
    l_warn("fcntl(%i, %i, ...): not implemented", fd, cmd);
    return 0;
}

int ioctl_soloader(int fd, int request, ...) {
    l_warn("ioctl(%i, %i, ...): not implemented", fd, request);
    return 0;
}

int fsync_soloader(int fd) {
    int ret = fsync(fd);
    l_debug("fsync(%i): %i", fd, ret);
    return ret;
}
