#include "utils/init.h"

#include "utils/dialog.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/settings.h"

#include <reimpl/controls.h>

#include <string.h>
#include <stdint.h>

#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/power.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#ifdef USE_SCELIBC_IO
#include <fios/fios.h>
#endif

#define LOAD_ADDRESS 0x98000000

extern so_module so_mod;

static void bootlog(const char *msg) {
    SceUID fd = sceIoOpen(DATA_PATH "loader.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, msg, (SceSize)strlen(msg));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
    sceClibPrintf("[rb4] %s\n", msg);
}

void soloader_init_all() {
    bootlog("init: start");

    /* Skip sceAppUtil — it has crashed early on some softfp builds. */

    bootlog("init: clocks");
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    bootlog("init: clocks ok");

#ifdef USE_SCELIBC_IO
    bootlog("init: fios");
    if (fios_init(DATA_PATH) == 0)
        bootlog("init: fios ok");
    else
        bootlog("init: fios failed (continuing)");
#endif

    /*
     * Skip module_loaded("kubridge") — _vshKernelSearchModuleByName can
     * hard-crash on some firmwares/stubs. so_file_load still needs kubridge;
     * if it's missing we'll fail at load and log that.
     */
    bootlog("init: check so");
    if (!file_exists(SO_PATH)) {
        bootlog("init: FATAL missing so");
        fatal_error("Missing data file:\n%s\n\nCopy libcocos2dcpp.so and game.apk to ux0:data/rdball4/", SO_PATH);
    }
    bootlog("init: so present");

    bootlog("init: so_file_load");
    if (so_file_load(&so_mod, SO_PATH, LOAD_ADDRESS) < 0) {
        bootlog("init: FATAL so_file_load (is kubridge.skprx installed?)");
        fatal_error("Error: could not load %s.\n\nIf kubridge is missing, add kubridge.skprx under *KERNEL in ur0:tai/config.txt and reboot.", SO_PATH);
    }
    bootlog("init: so_file_load ok");

    bootlog("init: settings_load");
    settings_load();
    bootlog("init: settings_load ok");

    bootlog("init: so_relocate");
    so_relocate(&so_mod);
    bootlog("init: so_relocate ok");

    bootlog("init: resolve_imports");
    resolve_imports(&so_mod);
    bootlog("init: resolve_imports ok");

    bootlog("init: so_patch");
    so_patch();
    bootlog("init: so_patch ok");

    bootlog("init: so_flush_caches");
    so_flush_caches(&so_mod);
    bootlog("init: so_flush_caches ok");

    bootlog("init: so_initialize");
    so_initialize(&so_mod);
    bootlog("init: so_initialize ok");

    bootlog("init: gl_preload");
    gl_preload();
    bootlog("init: gl_preload ok");

    bootlog("init: jni_init");
    jni_init();
    bootlog("init: jni_init ok");

#ifndef NDK_PORT
    bootlog("init: controls_init");
    controls_init();
    bootlog("init: controls_init ok");
#endif
    bootlog("init: done");
}
