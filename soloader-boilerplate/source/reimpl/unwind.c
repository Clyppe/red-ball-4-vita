/*
 * ARM EHABI exception index lookup for the loaded Android .so.
 * Without this, any C++ throw in the game ends in std::terminate/abort.
 */

#include <stdint.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>

#include <so_util/so_util.h>

extern so_module so_mod;

/* Main eboot exidx provided by the toolchain. */
extern char __exidx_start[];
extern char __exidx_end[];

typedef uintptr_t _Unwind_Ptr;

_Unwind_Ptr __gnu_Unwind_Find_exidx(_Unwind_Ptr pc, int *pcount) {
    uintptr_t addr = (uintptr_t)pc & ~1u;

    if (so_mod.exidx_base && so_mod.exidx_size &&
        so_mod.text_base && so_mod.text_size &&
        addr >= so_mod.text_base &&
        addr < so_mod.text_base + so_mod.text_size) {
        if (pcount)
            *pcount = (int)(so_mod.exidx_size / 8);
        return (_Unwind_Ptr)so_mod.exidx_base;
    }

    if (pcount)
        *pcount = (int)((__exidx_end - __exidx_start) / 8);
    return (_Unwind_Ptr)__exidx_start;
}

/* Bionic alias used by some Android libcs. */
_Unwind_Ptr dl_unwind_find_exidx(_Unwind_Ptr pc, int *pcount) {
    return __gnu_Unwind_Find_exidx(pc, pcount);
}

/* Log exception type before the real throw (helps when unwind still fails). */
extern void __cxa_throw(void *thrown_exception, void *tinfo, void (*dest)(void *));

void __cxa_throw_soloader(void *thrown_exception, void *tinfo, void (*dest)(void *)) {
    const char *name = "(unknown)";
    if (tinfo) {
        /* Itanium type_info: vptr then name pointer */
        const char *n = *(const char **)((char *)tinfo + sizeof(void *));
        if (n && n[0])
            name = n;
    }

    char msg[192];
    int n = sceClibSnprintf(msg, sizeof(msg), "FATAL: __cxa_throw type=%s\n", name);
    SceUID fd = sceIoOpen("ux0:data/ctr/loader.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        if (n > 0)
            sceIoWrite(fd, msg, (SceSize)n);
        sceIoClose(fd);
    }

    __cxa_throw(thrown_exception, tinfo, dest);
}
