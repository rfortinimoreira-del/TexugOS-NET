#ifndef TEXUGOS_SNAKE_H
#define TEXUGOS_SNAKE_H

#include <stdint.h>

void snake_initialize(void);
void snake_draw(int32_t x, int32_t y, uint32_t width, uint32_t height);
void snake_handle_key(char key);

#endif
