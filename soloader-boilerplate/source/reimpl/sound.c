/*
 * Minimal SoundPool/MediaPlayer stand-in for Cut the Rope JNI SoundMgr.
 * Decodes OGG via vorbisfile (sceIo → memory) and mixes to sceAudioOut.
 */

#include "reimpl/sound.h"

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>

#include <vorbis/vorbisfile.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* BGM port accepts 44100; matches CTR music and avoids heavy resample. */
#define SAMPLE_RATE 44100
#define MAX_SOUNDS  128
#define MAX_VOICES  12
#define MIX_FRAMES  512

typedef struct {
    int id;
    int channels;
    int frames;
    int16_t *pcm; /* interleaved stereo at SAMPLE_RATE */
    int used;
} SoundSample;

typedef struct {
    int active;
    int sound_index;
    int pos;
    int loop;
} Voice;

static SoundSample g_sounds[MAX_SOUNDS];
static Voice g_voices[MAX_VOICES];
static int g_audio_port = -1;
static int g_thread_id = -1;
static volatile int g_running = 0;
static volatile int g_suspended = 0;
static SceKernelLwMutexWork g_lock;
static int g_lock_ready = 0;
static int g_music_active = 0;
static int g_music_paused = 0;
static char g_music_pending[256];
static volatile int g_music_loading = 0;
static SceUID g_music_load_thid = -1;

static void slog(const char *msg) {
    SceUID fd = sceIoOpen(DATA_PATH "loader.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, msg, (SceSize)strlen(msg));
        sceIoClose(fd);
    }
}

static void lock(void) {
    if (!g_lock_ready) {
        sceKernelCreateLwMutex(&g_lock, "ctr_snd", 0, 0, NULL);
        g_lock_ready = 1;
    }
    sceKernelLockLwMutex(&g_lock, 1, NULL);
}

static void unlock(void) {
    sceKernelUnlockLwMutex(&g_lock, 1);
}

static int find_sound(int id) {
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (g_sounds[i].used && g_sounds[i].id == id)
            return i;
    }
    return -1;
}

static int alloc_sound_slot(void) {
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (!g_sounds[i].used)
            return i;
    }
    return -1;
}

typedef struct {
    const unsigned char *data;
    size_t size;
    size_t pos;
} MemFile;

/* Streamed BGM — avoids multi-second full OGG→PCM decode delay. */
typedef struct {
    unsigned char *data;
    size_t size;
    MemFile mf;
    OggVorbis_File vf;
    int open;
    int channels;
    int rate;
    char name[256];
} MusicStream;

static MusicStream g_music;

static size_t mem_read(void *ptr, size_t size, size_t nmemb, void *datasource) {
    MemFile *mf = (MemFile *)datasource;
    size_t want = size * nmemb;
    size_t left = mf->size - mf->pos;
    if (want > left)
        want = left;
    if (want)
        memcpy(ptr, mf->data + mf->pos, want);
    mf->pos += want;
    return size ? (want / size) : 0;
}

static int mem_seek(void *datasource, ogg_int64_t offset, int whence) {
    MemFile *mf = (MemFile *)datasource;
    ogg_int64_t np;
    if (whence == SEEK_SET) np = offset;
    else if (whence == SEEK_CUR) np = (ogg_int64_t)mf->pos + offset;
    else if (whence == SEEK_END) np = (ogg_int64_t)mf->size + offset;
    else return -1;
    if (np < 0 || (size_t)np > mf->size)
        return -1;
    mf->pos = (size_t)np;
    return 0;
}

static long mem_tell(void *datasource) {
    return (long)((MemFile *)datasource)->pos;
}

static int mem_close(void *datasource) {
    (void)datasource;
    return 0;
}

static unsigned char *read_file_bytes(const char *path, size_t *out_sz) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return NULL;
    SceOff sz = sceIoLseek(fd, 0, SCE_SEEK_END);
    if (sz <= 0 || sz > 16 * 1024 * 1024) {
        sceIoClose(fd);
        return NULL;
    }
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) {
        sceIoClose(fd);
        return NULL;
    }
    int rd = sceIoRead(fd, buf, (SceSize)sz);
    sceIoClose(fd);
    if (rd != (int)sz) {
        free(buf);
        return NULL;
    }
    *out_sz = (size_t)sz;
    return buf;
}

static int16_t *resample_to_stereo(const int16_t *in, int in_ch, int in_frames,
                                   int in_rate, int *out_frames) {
    if (in_rate <= 0 || in_frames <= 0)
        return NULL;

    /* Fast path: already 44100 stereo */
    if (in_rate == SAMPLE_RATE && in_ch == 2) {
        size_t bytes = (size_t)in_frames * 2 * sizeof(int16_t);
        int16_t *out = (int16_t *)malloc(bytes);
        if (!out)
            return NULL;
        memcpy(out, in, bytes);
        *out_frames = in_frames;
        return out;
    }

    /* Exact 2x upsample for common 22050 mono SFX */
    if (in_rate * 2 == SAMPLE_RATE && in_ch == 1) {
        int oframes = in_frames * 2;
        int16_t *out = (int16_t *)malloc((size_t)oframes * 2 * sizeof(int16_t));
        if (!out)
            return NULL;
        for (int i = 0; i < in_frames; i++) {
            int16_t s = in[i];
            out[(i * 2) * 2] = s;
            out[(i * 2) * 2 + 1] = s;
            out[(i * 2 + 1) * 2] = s;
            out[(i * 2 + 1) * 2 + 1] = s;
        }
        *out_frames = oframes;
        return out;
    }

    double ratio = (double)SAMPLE_RATE / (double)in_rate;
    int oframes = (int)((double)in_frames * ratio + 0.5);
    if (oframes < 1)
        oframes = 1;
    int16_t *out = (int16_t *)malloc((size_t)oframes * 2 * sizeof(int16_t));
    if (!out)
        return NULL;
    for (int i = 0; i < oframes; i++) {
        double src = (double)i / ratio;
        int i0 = (int)src;
        int i1 = i0 + 1;
        if (i0 >= in_frames) i0 = in_frames - 1;
        if (i1 >= in_frames) i1 = in_frames - 1;
        float t = (float)(src - (double)i0);
        int16_t l0, r0, l1, r1;
        if (in_ch == 1) {
            l0 = r0 = in[i0];
            l1 = r1 = in[i1];
        } else {
            l0 = in[i0 * 2];
            r0 = in[i0 * 2 + 1];
            l1 = in[i1 * 2];
            r1 = in[i1 * 2 + 1];
        }
        out[i * 2] = (int16_t)((1.f - t) * l0 + t * l1);
        out[i * 2 + 1] = (int16_t)((1.f - t) * r0 + t * r1);
    }
    *out_frames = oframes;
    return out;
}

static int decode_ogg_mem(const unsigned char *data, size_t size, SoundSample *out) {
    MemFile mf = { data, size, 0 };
    ov_callbacks cb = { mem_read, mem_seek, mem_close, mem_tell };
    OggVorbis_File vf;
    if (ov_open_callbacks(&mf, &vf, NULL, 0, cb) < 0)
        return -1;

    vorbis_info *vi = ov_info(&vf, -1);
    if (!vi || (vi->channels != 1 && vi->channels != 2)) {
        ov_clear(&vf);
        return -1;
    }

    ogg_int64_t total = ov_pcm_total(&vf, -1);
    if (total <= 0 || total > 30LL * SAMPLE_RATE * 60) {
        ov_clear(&vf);
        return -1;
    }

    int channels = vi->channels;
    int in_rate = vi->rate;
    size_t samples = (size_t)total * (size_t)channels;
    int16_t *pcm = (int16_t *)malloc(samples * sizeof(int16_t));
    if (!pcm) {
        ov_clear(&vf);
        return -1;
    }

    size_t got = 0;
    int bitstream = 0;
    while (got < samples) {
        long n = ov_read(&vf, (char *)(pcm + got),
                         (int)((samples - got) * sizeof(int16_t)),
                         0, 2, 1, &bitstream);
        if (n <= 0)
            break;
        got += (size_t)n / sizeof(int16_t);
    }
    ov_clear(&vf);

    int in_frames = (int)(got / (size_t)channels);
    int out_frames = 0;
    int16_t *stereo = resample_to_stereo(pcm, channels, in_frames, in_rate, &out_frames);
    free(pcm);
    if (!stereo)
        return -1;

    out->pcm = stereo;
    out->channels = 2;
    out->frames = out_frames;
    out->used = 1;
    return 0;
}

static int try_load_path(const char *name, SoundSample *out) {
    char paths[6][512];
    int n = 0;
    char with_ogg[512];

    if (strchr(name, ':')) {
        snprintf(paths[n++], sizeof(paths[0]), "%s", name);
    } else {
        snprintf(paths[n++], sizeof(paths[0]), DATA_PATH "assets/%s", name);
        snprintf(paths[n++], sizeof(paths[0]), DATA_PATH "%s", name);
        if (!strstr(name, ".ogg") && !strstr(name, ".OGG")) {
            snprintf(with_ogg, sizeof(with_ogg), "%s.ogg", name);
            snprintf(paths[n++], sizeof(paths[0]), DATA_PATH "assets/%s", with_ogg);
            snprintf(paths[n++], sizeof(paths[0]), DATA_PATH "%s", with_ogg);
        }
    }

    for (int i = 0; i < n; i++) {
        size_t sz = 0;
        unsigned char *buf = read_file_bytes(paths[i], &sz);
        if (!buf)
            continue;
        int ok = decode_ogg_mem(buf, sz, out);
        free(buf);
        if (ok == 0)
            return 0;
    }
    return -1;
}

static int mix_sample(const SoundSample *s, int *pos, int loop, int16_t *dst, int frames) {
    if (!s || !s->pcm || *pos >= s->frames)
        return 0;
    for (int i = 0; i < frames; i++) {
        if (*pos >= s->frames) {
            if (loop)
                *pos = 0;
            else
                return 0;
        }
        int16_t l = s->pcm[(*pos) * 2];
        int16_t r = s->pcm[(*pos) * 2 + 1];
        int32_t ol = (int32_t)dst[i * 2] + l;
        int32_t orr = (int32_t)dst[i * 2 + 1] + r;
        if (ol > 32767) ol = 32767;
        if (ol < -32768) ol = -32768;
        if (orr > 32767) orr = 32767;
        if (orr < -32768) orr = -32768;
        dst[i * 2] = (int16_t)ol;
        dst[i * 2 + 1] = (int16_t)orr;
        (*pos)++;
    }
    return 1;
}

static void music_close_locked(MusicStream *s) {
    if (!s)
        return;
    if (s->open) {
        ov_clear(&s->vf);
        s->open = 0;
    }
    if (s->data) {
        free(s->data);
        s->data = NULL;
    }
    s->size = 0;
    s->name[0] = 0;
}

static int music_open_bytes(MusicStream *s, unsigned char *data, size_t size, const char *name) {
    memset(s, 0, sizeof(*s));
    s->data = data;
    s->size = size;
    s->mf.data = data;
    s->mf.size = size;
    s->mf.pos = 0;
    ov_callbacks cb = { mem_read, mem_seek, mem_close, mem_tell };
    if (ov_open_callbacks(&s->mf, &s->vf, NULL, 0, cb) < 0) {
        free(data);
        memset(s, 0, sizeof(*s));
        return -1;
    }
    vorbis_info *vi = ov_info(&s->vf, -1);
    if (!vi || (vi->channels != 1 && vi->channels != 2) || vi->rate <= 0) {
        ov_clear(&s->vf);
        free(data);
        memset(s, 0, sizeof(*s));
        return -1;
    }
    s->channels = vi->channels;
    s->rate = vi->rate;
    s->open = 1;
    snprintf(s->name, sizeof(s->name), "%s", name ? name : "");
    return 0;
}

/* Caller holds lock. Streams RB4 music (already 44100 stereo). */
static void mix_music_stream(int16_t *dst, int frames) {
    if (!g_music_active || g_music_paused || !g_music.open)
        return;

    int need = frames * g_music.channels;
    static int16_t tmp[MIX_FRAMES * 2];
    int got = 0;
    int bitstream = 0;

    while (got < need) {
        long n = ov_read(&g_music.vf, (char *)(tmp + got),
                         (int)((need - got) * (int)sizeof(int16_t)),
                         0, 2, 1, &bitstream);
        if (n <= 0) {
            if (ov_pcm_seek(&g_music.vf, 0) != 0)
                break;
            continue;
        }
        got += (int)(n / (long)sizeof(int16_t));
    }

    if (g_music.channels == 2 && g_music.rate == SAMPLE_RATE) {
        int samples = got / 2;
        if (samples > frames)
            samples = frames;
        for (int i = 0; i < samples; i++) {
            int32_t ol = (int32_t)dst[i * 2] + tmp[i * 2];
            int32_t orr = (int32_t)dst[i * 2 + 1] + tmp[i * 2 + 1];
            if (ol > 32767) ol = 32767;
            if (ol < -32768) ol = -32768;
            if (orr > 32767) orr = 32767;
            if (orr < -32768) orr = -32768;
            dst[i * 2] = (int16_t)ol;
            dst[i * 2 + 1] = (int16_t)orr;
        }
    } else if (g_music.channels == 1 && g_music.rate == SAMPLE_RATE) {
        int samples = got;
        if (samples > frames)
            samples = frames;
        for (int i = 0; i < samples; i++) {
            int16_t s = tmp[i];
            int32_t ol = (int32_t)dst[i * 2] + s;
            int32_t orr = (int32_t)dst[i * 2 + 1] + s;
            if (ol > 32767) ol = 32767;
            if (ol < -32768) ol = -32768;
            if (orr > 32767) orr = 32767;
            if (orr < -32768) orr = -32768;
            dst[i * 2] = (int16_t)ol;
            dst[i * 2 + 1] = (int16_t)orr;
        }
    }
}

static int audio_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    /* Vita audio DMA prefers aligned buffers. */
    static int16_t mix[MIX_FRAMES * 2] __attribute__((aligned(64)));

    while (g_running) {
        memset(mix, 0, sizeof(mix));
        if (!g_suspended && g_audio_port >= 0) {
            lock();
            for (int v = 0; v < MAX_VOICES; v++) {
                if (!g_voices[v].active)
                    continue;
                int si = g_voices[v].sound_index;
                if (si < 0 || !g_sounds[si].used) {
                    g_voices[v].active = 0;
                    continue;
                }
                if (!mix_sample(&g_sounds[si], &g_voices[v].pos, g_voices[v].loop, mix, MIX_FRAMES))
                    g_voices[v].active = 0;
            }
            mix_music_stream(mix, MIX_FRAMES);
            unlock();
        }
        if (g_audio_port >= 0)
            sceAudioOutOutput(g_audio_port, mix);
        else
            sceKernelDelayThread(10000);
    }
    return 0;
}

void sound_init(void) {
    if (g_audio_port >= 0)
        return;
    /* BGM accepts 44100 and matches menu_music.ogg — avoids resample distortion. */
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, MIX_FRAMES, SAMPLE_RATE,
                                   SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "sound_init FAIL port=0x%08x\n", (unsigned)port);
        slog(msg);
        return;
    }
    g_audio_port = port;
    int vols[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
    sceAudioOutSetVolume(g_audio_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols);

    g_running = 1;
    g_thread_id = sceKernelCreateThread("ctr_audio", audio_thread, 0x10000100, 0x10000, 0, 0, NULL);
    if (g_thread_id >= 0) {
        sceKernelStartThread(g_thread_id, 0, NULL);
        slog("sound_init ok BGM@44100\n");
    } else {
        slog("sound_init FAIL thread\n");
    }
}

void sound_load(const char *name, int id) {
    sound_init();
    SoundSample tmp;
    memset(&tmp, 0, sizeof(tmp));
    if (try_load_path(name, &tmp) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "sound_load FAIL id=%d \"%s\"\n", id, name);
        slog(msg);
        return;
    }

    lock();
    int idx = find_sound(id);
    if (idx < 0)
        idx = alloc_sound_slot();
    if (idx < 0) {
        unlock();
        free(tmp.pcm);
        return;
    }
    if (g_sounds[idx].pcm)
        free(g_sounds[idx].pcm);
    g_sounds[idx] = tmp;
    g_sounds[idx].id = id;
    unlock();
}

void sound_play_looped(int id, int loop) {
    sound_init();
    lock();
    int si = find_sound(id);
    if (si < 0) {
        unlock();
        return;
    }
    int slot = -1;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!g_voices[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        slot = 0;
    g_voices[slot].active = 1;
    g_voices[slot].sound_index = si;
    g_voices[slot].pos = 0;
    g_voices[slot].loop = loop ? 1 : 0;
    unlock();
}

void sound_stop(int id, int unused) {
    (void)unused;
    lock();
    int si = find_sound(id);
    if (si >= 0) {
        for (int i = 0; i < MAX_VOICES; i++) {
            if (g_voices[i].active && g_voices[i].sound_index == si)
                g_voices[i].active = 0;
        }
    }
    unlock();
}

void sound_stop_all(void) {
    lock();
    for (int i = 0; i < MAX_VOICES; i++)
        g_voices[i].active = 0;
    unlock();
}

static int music_resolve_and_read(const char *name, unsigned char **out_data, size_t *out_sz) {
    char paths[6][512];
    int n = 0;
    char with_ogg[512];

    if (strchr(name, ':')) {
        snprintf(paths[n++], sizeof(paths[0]), "%s", name);
    } else {
        snprintf(paths[n++], sizeof(paths[0]), DATA_PATH "assets/%s", name);
        snprintf(paths[n++], sizeof(paths[0]), DATA_PATH "%s", name);
        if (!strstr(name, ".ogg") && !strstr(name, ".OGG")) {
            snprintf(with_ogg, sizeof(with_ogg), "%s.ogg", name);
            snprintf(paths[n++], sizeof(paths[0]), DATA_PATH "assets/%s", with_ogg);
            snprintf(paths[n++], sizeof(paths[0]), DATA_PATH "%s", with_ogg);
        }
    }

    for (int i = 0; i < n; i++) {
        size_t sz = 0;
        unsigned char *buf = read_file_bytes(paths[i], &sz);
        if (buf) {
            *out_data = buf;
            *out_sz = sz;
            return 0;
        }
    }
    return -1;
}

static int music_load_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (;;) {
        char name[256];
        lock();
        snprintf(name, sizeof(name), "%s", g_music_pending);
        unlock();

        unsigned char *data = NULL;
        size_t sz = 0;
        int read_ok = music_resolve_and_read(name, &data, &sz);

        lock();
        /* Open into g_music in-place so OggVorbis_File's datasource stays valid. */
        if (read_ok == 0 && data) {
            g_music_active = 0;
            music_close_locked(&g_music);
            if (music_open_bytes(&g_music, data, sz, name) == 0) {
                if (strcmp(g_music_pending, name) == 0) {
                    g_music_active = 1;
                    g_music_paused = 0;
                }
                slog("sound_play_music stream ok\n");
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "sound_play_music FAIL \"%s\"\n", name);
                slog(msg);
            }
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "sound_play_music FAIL \"%s\"\n", name);
            slog(msg);
        }

        if (strcmp(g_music_pending, name) != 0) {
            unlock();
            continue;
        }
        g_music_loading = 0;
        g_music_load_thid = -1;
        unlock();
        break;
    }
    return sceKernelExitDeleteThread(0);
}

void sound_play_music(const char *name) {
    sound_init();
    if (!name || !name[0])
        return;

    lock();
    if (g_music.open && strcmp(g_music.name, name) == 0) {
        ov_pcm_seek(&g_music.vf, 0);
        g_music_active = 1;
        g_music_paused = 0;
        unlock();
        return;
    }
    snprintf(g_music_pending, sizeof(g_music_pending), "%s", name);
    /* Cut current stream immediately; replacement opens on loader thread. */
    g_music_active = 0;
    if (g_music_loading) {
        unlock();
        return;
    }
    g_music_loading = 1;
    unlock();

    g_music_load_thid = sceKernelCreateThread("rb4_music", music_load_thread, 0x10000100 + 10, 0x10000, 0, 0, NULL);
    if (g_music_load_thid >= 0) {
        sceKernelStartThread(g_music_load_thid, 0, NULL);
    } else {
        unsigned char *data = NULL;
        size_t sz = 0;
        if (music_resolve_and_read(name, &data, &sz) == 0) {
            lock();
            music_close_locked(&g_music);
            if (music_open_bytes(&g_music, data, sz, name) == 0) {
                g_music_active = 1;
                g_music_paused = 0;
            }
            g_music_loading = 0;
            unlock();
        } else {
            g_music_loading = 0;
        }
    }
}

void sound_stop_music(void) {
    lock();
    g_music_active = 0;
    g_music_paused = 0;
    unlock();
}

void sound_pause_music(void) {
    lock();
    if (g_music_active) {
        g_music_active = 0;
        g_music_paused = 1;
    }
    unlock();
}

void sound_resume_music(void) {
    lock();
    if (g_music_paused && g_music.open) {
        g_music_active = 1;
        g_music_paused = 0;
    }
    unlock();
}

int sound_is_music_playing(void) {
    return (g_music_active && g_music.open) ? 1 : 0;
}

static int path_id(const char *name) {
    unsigned h = 5381;
    const unsigned char *p = (const unsigned char *)(name ? name : "");
    while (*p)
        h = ((h << 5) + h) + *p++;
    int id = (int)(h & 0x7fffffff);
    return id ? id : 1;
}

int sound_play_effect(const char *name, int loop) {
    int id = path_id(name);
    sound_init();
    lock();
    int si = find_sound(id);
    unlock();
    if (si < 0)
        sound_load(name, id);
    sound_play_looped(id, loop);
    return id;
}

void sound_preload(const char *name) {
    sound_load(name, path_id(name));
}

void sound_suspend(void) {
    g_suspended = 1;
    sound_stop_all();
    sound_stop_music();
}

void sound_resume(void) {
    g_suspended = 0;
}
