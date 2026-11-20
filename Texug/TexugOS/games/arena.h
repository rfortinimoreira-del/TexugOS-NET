#ifndef TEXUGOS_ARENA_H
#define TEXUGOS_ARENA_H

#include <stdint.h>

void arena_initialize(void);
void arena_draw(int32_t x, int32_t y, uint32_t width, uint32_t height);
void arena_handle_key(char key);
void arena_handle_mouse(int32_t delta_x);
void arena_fire(void);

#endif
