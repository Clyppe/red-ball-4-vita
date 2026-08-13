#ifndef RB4_PREFS_H
#define RB4_PREFS_H

void prefs_init(void);
void prefs_flush(void); /* write dirty prefs once (call from game loop) */
int prefs_get_bool(const char *key, int def);
int prefs_get_int(const char *key, int def);
float prefs_get_float(const char *key, float def);
const char *prefs_get_string(const char *key, const char *def);
void prefs_set_bool(const char *key, int val);
void prefs_set_int(const char *key, int val);
void prefs_set_float(const char *key, float val);
void prefs_set_string(const char *key, const char *val);

#endif
