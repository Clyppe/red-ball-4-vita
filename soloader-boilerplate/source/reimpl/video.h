#ifndef RB4_VIDEO_H
#define RB4_VIDEO_H

#include <falso_jni/FalsoJNI.h>
#include <stdarg.h>

void video_init(void);
/* 1 while a cutscene is on screen (main loop should not nativeRender). */
int video_is_playing(void);
/* Draw current frame / finish when done. Call instead of nativeRender while playing. */
void video_frame(void);

void java_playVideo(jmethodID id, va_list args);
void java_playNewEpisodeVideo(jmethodID id, va_list args);
void java_playRefillVideo(jmethodID id, va_list args);
void java_showAfterLevel(jmethodID id, va_list args);
void java_showAd(jmethodID id, va_list args);
void java_showBootupAd(jmethodID id, va_list args);

jboolean java_appReturnedFromClosedAd(jmethodID id, va_list args);
jboolean java_appReturnedFromClosedVideo(jmethodID id, va_list args);
jboolean java_appReturnedFromClosedPermission(jmethodID id, va_list args);

#endif
