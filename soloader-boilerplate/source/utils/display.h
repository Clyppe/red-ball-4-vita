#ifndef SOLOADER_DISPLAY_H
#define SOLOADER_DISPLAY_H

#include <vitaGL.h>

#define CTR_PHYS_W 960
#define CTR_PHYS_H 544

/* Red Ball 4 is landscape; match the Vita framebuffer (no letterbox). */
#define CTR_GAME_W 960
#define CTR_GAME_H 544

void display_layout_init(void);
void display_touch_to_game(float vx, float vy, float *gx, float *gy);
void display_apply_default_viewport(void);

void glViewport_soloader(GLint x, GLint y, GLsizei w, GLsizei h);
void glScissor_soloader(GLint x, GLint y, GLsizei w, GLsizei h);

#endif
