#ifndef SOLOADER_SOUND_H
#define SOLOADER_SOUND_H

void sound_init(void);
void sound_load(const char *name, int id);
void sound_play_looped(int id, int loop);
void sound_stop(int id, int unused);
void sound_stop_all(void);
void sound_play_music(const char *name);
void sound_stop_music(void);
void sound_pause_music(void);
void sound_resume_music(void);
int sound_is_music_playing(void);
int sound_play_effect(const char *name, int loop);
void sound_preload(const char *name);
void sound_suspend(void);
void sound_resume(void);

#endif
