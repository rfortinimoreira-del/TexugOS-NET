#ifndef TEXUGOS_PAINT_H
#define TEXUGOS_PAINT_H

#include <stdint.h>

void paint_initialize(void);
void paint_draw(int32_t x, int32_t y, uint32_t width, uint32_t height);
void paint_handle_pointer(int32_t x, int32_t y, uint32_t buttons);
void paint_handle_click(int32_t x, int32_t y,
                        uint32_t width, uint32_t height);

#endif
