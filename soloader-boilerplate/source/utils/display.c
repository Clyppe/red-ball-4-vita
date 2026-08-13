#include "utils/display.h"

static float g_off_x;
static float g_off_y;
static float g_view_w;
static float g_view_h;

void display_layout_init(void) {
    float sx = (float)CTR_PHYS_W / (float)CTR_GAME_W;
    float sy = (float)CTR_PHYS_H / (float)CTR_GAME_H;
    float s = sx < sy ? sx : sy;
    g_view_w = (float)CTR_GAME_W * s;
    g_view_h = (float)CTR_GAME_H * s;
    g_off_x = ((float)CTR_PHYS_W - g_view_w) * 0.5f;
    /* GL y=0 is bottom; center vertically in framebuffer space. */
    g_off_y = ((float)CTR_PHYS_H - g_view_h) * 0.5f;
}

void display_apply_default_viewport(void) {
    glViewport_soloader(0, 0, CTR_GAME_W, CTR_GAME_H);
    glScissor_soloader(0, 0, CTR_GAME_W, CTR_GAME_H);
}

void display_touch_to_game(float vx, float vy, float *gx, float *gy) {
    /* Front touch: y grows downward. Map through the letterboxed rect. */
    float x = (vx - g_off_x) * ((float)CTR_GAME_W / g_view_w);
    float y = (vy - g_off_y) * ((float)CTR_GAME_H / g_view_h);
    if (x < 0.f) x = 0.f;
    if (y < 0.f) y = 0.f;
    if (x > (float)CTR_GAME_W) x = (float)CTR_GAME_W;
    if (y > (float)CTR_GAME_H) y = (float)CTR_GAME_H;
    *gx = x;
    *gy = y;
}

void glViewport_soloader(GLint x, GLint y, GLsizei w, GLsizei h) {
    float nx = g_off_x + (float)x * (g_view_w / (float)CTR_GAME_W);
    float ny = g_off_y + (float)y * (g_view_h / (float)CTR_GAME_H);
    float nw = (float)w * (g_view_w / (float)CTR_GAME_W);
    float nh = (float)h * (g_view_h / (float)CTR_GAME_H);
    if (nw < 1.f) nw = 1.f;
    if (nh < 1.f) nh = 1.f;
    glViewport((GLint)(nx + 0.5f), (GLint)(ny + 0.5f),
               (GLsizei)(nw + 0.5f), (GLsizei)(nh + 0.5f));
}

void glScissor_soloader(GLint x, GLint y, GLsizei w, GLsizei h) {
    float nx = g_off_x + (float)x * (g_view_w / (float)CTR_GAME_W);
    float ny = g_off_y + (float)y * (g_view_h / (float)CTR_GAME_H);
    float nw = (float)w * (g_view_w / (float)CTR_GAME_W);
    float nh = (float)h * (g_view_h / (float)CTR_GAME_H);
    if (nw < 1.f) nw = 1.f;
    if (nh < 1.f) nh = 1.f;
    glScissor((GLint)(nx + 0.5f), (GLint)(ny + 0.5f),
              (GLsizei)(nw + 0.5f), (GLsizei)(nh + 0.5f));
}
