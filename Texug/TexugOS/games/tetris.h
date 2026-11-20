#ifndef TEXUGOS_TETRIS_H
#define TEXUGOS_TETRIS_H

#include <stdint.h>

void tetris_initialize(void);
void tetris_draw(int32_t x, int32_t y, uint32_t width, uint32_t height);
void tetris_handle_key(char key);

#endif
