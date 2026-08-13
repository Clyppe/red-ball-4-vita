/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/sys.h"

#include <sys/errno.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <string.h>
#include <stdint.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/rtc.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils/utils.h"
#include "utils/logger.h"

#define BIONIC_CLOCK_REALTIME           0
#define BIONIC_CLOCK_MONOTONIC          1
#define BIONIC_CLOCK_PROCESS_CPUTIME_ID 2
#define BIONIC_CLOCK_THREAD_CPUTIME_ID  3
#define BIONIC_CLOCK_MONOTONIC_RAW      4
#define BIONIC_CLOCK_REALTIME_COARSE    5
#define BIONIC_CLOCK_MONOTONIC_COARSE   6
#define BIONIC_CLOCK_BOOTTIME           7
#define BIONIC_CLOCK_REALTIME_ALARM     8
#define BIONIC_CLOCK_BOOTTIME_ALARM     9
#define BIONIC_CLOCK_SGI_CYCLE         10
#define BIONIC_CLOCK_TAI               11

// 1969 years in microseconds, used to adjust SCE tick to UNIX timestamp
#define __epoch 62135587294000000

int clock_gettime_soloader(clockid_t clock_id, struct timespec * tp) {
    switch (clock_id) {
        case BIONIC_CLOCK_MONOTONIC:
        case BIONIC_CLOCK_MONOTONIC_RAW:
        case BIONIC_CLOCK_MONOTONIC_COARSE:
        case BIONIC_CLOCK_BOOTTIME:
        case BIONIC_CLOCK_BOOTTIME_ALARM:
        case BIONIC_CLOCK_SGI_CYCLE:
        case BIONIC_CLOCK_PROCESS_CPUTIME_ID:
        case BIONIC_CLOCK_THREAD_CPUTIME_ID: {
            uint64_t proctime = sceKernelGetProcessTimeWide();

            tp->tv_sec = (proctime / 1000000);
            tp->tv_nsec = ((proctime - (tp->tv_sec * 1000000)) * 1000);
            break;
        }
        case BIONIC_CLOCK_REALTIME:
        case BIONIC_CLOCK_REALTIME_COARSE:
        case BIONIC_CLOCK_REALTIME_ALARM:
        case BIONIC_CLOCK_TAI: {
            SceRtcTick tick;
            sceRtcGetCurrentTick(&tick);
            tick.tick -= __epoch;

            tp->tv_sec = (tick.tick / 1000000);
            tp->tv_nsec = ((tick.tick - (tp->tv_sec * 1000000)) * 1000);
            break;
        }
        default:
            l_error("clock_gettime / unexpected clock id %i", clock_id);
    }

    return 0;
}

int clock_getres_soloader(clockid_t clock_id, struct timespec * res) {
    res->tv_sec = 0;
    res->tv_nsec = 1000;
    return 0;
}

clock_t clock_soloader(void) {
    return sceKernelGetProcessTimeLow();
}

int sigaction(int signum, const struct sigaction * act, struct sigaction * oldact) {
    l_warn("sigaction(%i, ...): not implemented", signum);
    return 0;
}

int __system_property_get_soloader(const char *name, char *value) {
    l_warn("__system_property_get(%s, %p): not implemented", name, value);
    strncpy(value, "psvita", 7);
    return 7;
}

void assert2(const char* f, int l, const char* func, const char* msg) {
    l_fatal("[%s:%i][%s] Assertion failed: %s", f, l, func, msg);
}

void syscall(int c) {
    l_warn("syscall(%i): not implemented", c);
}

void __stack_chk_fail_soloader() {
    l_fatal("Stack collapsed at address %p", __builtin_return_address(0));
}

void abort_soloader() {
    void *ra = __builtin_return_address(0);
    char msg[96];
    int n = sceClibSnprintf(msg, sizeof(msg), "FATAL: abort() called from %p\n", ra);
    SceUID fd = sceIoOpen("ux0:data/ctr/loader.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        if (n > 0)
            sceIoWrite(fd, msg, (SceSize)n);
        sceIoClose(fd);
    }
    l_fatal("Abort called from address %p", ra);
    sceKernelExitProcess(1);
    while (1) {}
}

void exit_soloader(int status) {
    l_fatal("Exit(%i) called from %p", status, __builtin_return_address(0));
    exit(status);
}

int __atomic_dec(volatile int *ptr) {
    return __sync_fetch_and_sub(ptr, 1);
}

int __atomic_inc(volatile int *ptr) {
    return __sync_fetch_and_add(ptr, 1);
}

int __atomic_swap(int new_value, volatile int *ptr) {
    int old_value;
    do {
        old_value = *ptr;
    } while (__sync_val_compare_and_swap(ptr, old_value, new_value) != old_value);
    return old_value;
}

int __atomic_cmpxchg(int old_value, int new_value, volatile int* ptr) {
    /* We must return 0 on success */
    return __sync_val_compare_and_swap(ptr, old_value, new_value) != old_value;
}

char * getenv_soloader(const char * var) {
    l_warn("getenv(\"%s\"): not implemented.", var);
    return NULL;
}

int setenv_soloader(const char * name, const char * value, int overwrite) {
    l_warn("setenv(\"%s\", \"%s\"): not implemented.", name, value);
    return 0;
}

int getpagesize(void) {
    return PAGE_SIZE;
}

int uname_soloader(void *buf) {
    struct utsname_soloader {
        char sysname[65];
        char nodename[65];
        char release[65];
        char version[65];
        char machine[65];
    } *u = (struct utsname_soloader *)buf;
    if (!u)
        return -1;
    memset(u, 0, sizeof(*u));
    strncpy(u->sysname, "Linux", sizeof(u->sysname) - 1);
    strncpy(u->nodename, "vita", sizeof(u->nodename) - 1);
    strncpy(u->release, "3.0.0", sizeof(u->release) - 1);
    strncpy(u->version, "#1", sizeof(u->version) - 1);
    strncpy(u->machine, "armv7l", sizeof(u->machine) - 1);
    return 0;
}

ssize_t writev_soloader(int fd, const void *iov, int iovcnt) {
    const struct iovec_soloader {
        void *iov_base;
        size_t iov_len;
    } *v = (const struct iovec_soloader *)iov;
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        ssize_t n = write(fd, v[i].iov_base, v[i].iov_len);
        if (n < 0)
            return n;
        total += n;
    }
    return total;
}

int sigprocmask_soloader(int how, const void *set, void *oldset) {
    (void)how;
    (void)set;
    (void)oldset;
    return 0;
}

