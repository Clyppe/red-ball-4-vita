/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  patch.c
 * @brief Patching some of the .so internal functions or bridging them to native
 *        for better compatibility.
 */

#include <kubridge.h>
#include <so_util/so_util.h>
#include <stdio.h>
#include <string.h>
#include <vitasdk.h>

#ifdef __cplusplus
extern "C"
{
#endif
	extern so_module so_mod;
#ifdef __cplusplus
};
#endif

#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RX (0x0C20D050)

#include "utils/logger.h"
#include "utils/dialog.h"
#include "reimpl/sys.h"
#include <stdbool.h>

void __kuser_memory_barrier(void) {
	__sync_synchronize();
}

void kuser_patch(void) {
	SceKernelAllocMemBlockKernelOpt opt;
	memset(&opt, 0, sizeof(SceKernelAllocMemBlockKernelOpt));
	opt.size = sizeof(SceKernelAllocMemBlockKernelOpt);
	opt.attr = 0x1;
	opt.field_C = (SceUInt32)0x9A000000;
	if (kuKernelAllocMemBlock("atomic", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, 0x1000, &opt) < 0)
		fatal_error("Error could not allocate atomic block.");
	kuKernelMemProtect((void *)0x9A000000, (SceSize)0x1000, KU_KERNEL_PROT_EXEC | KU_KERNEL_PROT_READ | KU_KERNEL_PROT_WRITE);

	hook_addr(0x9A000FA0, (uintptr_t)__kuser_memory_barrier);
	hook_addr(0x9A000FC0, (uintptr_t)__atomic_cmpxchg);

	uint32_t patched_addr;
	for (uint32_t addr = so_mod.text_base; addr < so_mod.text_base + so_mod.text_size; addr += 4) {
		uint32_t *a = (uint32_t *)addr;
		if (*a == 0xFFFF0FC0) {
			l_debug("Patching 0x%x -> __kuser_cmpxchg", a);
			patched_addr = 0x9A000FC0;
			kuKernelCpuUnrestrictedMemcpy((void *)(addr), &patched_addr, sizeof(uint32_t));
		}
		else if (*a == 0xFFFF0FA0) {
			l_debug("Patching 0x%x -> __kuser_memory_barrier", a);
			patched_addr = 0x9A000FA0;
			kuKernelCpuUnrestrictedMemcpy((void *)(addr), &patched_addr, sizeof(uint32_t));
		}
	}
}

static int nativeutils_return_false(void) {
	return 0;
}

static void progress_select_age_nop(void) {
}

static so_hook h_getBool;
static so_hook h_getInt;

static int hooked_getBool(void *self, const char *key, int def) {
	if (key && strstr(key, "player_age_selected"))
		return 1;
	return SO_CONTINUE(int, h_getBool, self, key, def);
}

static int hooked_getInt(void *self, const char *key, int def) {
	if (key && strcmp(key, "RedBall4_player_age") == 0)
		return 99;
	return SO_CONTINUE(int, h_getInt, self, key, def);
}

static void hook_log(const char *msg) {
	SceUID fd = sceIoOpen(DATA_PATH "loader.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
	if (fd >= 0) {
		sceIoWrite(fd, msg, (SceSize)strlen(msg));
		sceIoWrite(fd, "\n", 1);
		sceIoClose(fd);
	}
}

static void hook_sym(const char *name, uintptr_t dst) {
	uintptr_t addr = so_symbol(&so_mod, name);
	char msg[192];
	if (addr) {
		hook_addr(addr, dst);
		snprintf(msg, sizeof(msg), "hook ok %s -> 0x%08x", name, (unsigned)addr);
	} else {
		snprintf(msg, sizeof(msg), "hook MISS %s", name);
	}
	hook_log(msg);
}

void so_patch(void) {
	kuser_patch();
	hook_sym("_ZN11NativeUtils4isTVEv", (uintptr_t)nativeutils_return_false);
	hook_sym("_ZN11NativeUtils6isTab4Ev", (uintptr_t)nativeutils_return_false);
	hook_sym("_ZN8Progress9selectAgeEv", (uintptr_t)progress_select_age_nop);

	uintptr_t a;
	a = so_symbol(&so_mod, "_ZN7cocos2d13CCUserDefault13getBoolForKeyEPKcb");
	if (a) {
		h_getBool = hook_addr(a, (uintptr_t)hooked_getBool);
		hook_log("hook ok getBoolForKey(key,bool)");
	} else {
		hook_log("hook MISS getBoolForKey(key,bool)");
	}
	a = so_symbol(&so_mod, "_ZN7cocos2d13CCUserDefault16getIntegerForKeyEPKci");
	if (a) {
		h_getInt = hook_addr(a, (uintptr_t)hooked_getInt);
		hook_log("hook ok getIntegerForKey(key,int)");
	} else {
		hook_log("hook MISS getIntegerForKey(key,int)");
	}
}