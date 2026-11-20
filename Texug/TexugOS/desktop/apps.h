#ifndef TEXUGOS_DESKTOP_APPS_H
#define TEXUGOS_DESKTOP_APPS_H

#include <stdint.h>

void desktop_apps_initialize(void);
void desktop_app_draw(uint32_t index, int32_t x, int32_t y,
                      uint32_t width, uint32_t height);
void desktop_app_handle_key(uint32_t index, char key);
void desktop_app_handle_mouse(uint32_t index, int32_t delta_x);
void desktop_app_handle_pointer(uint32_t index, int32_t x, int32_t y,
                                uint32_t buttons);
void desktop_app_handle_click(uint32_t index, int32_t x, int32_t y,
                              uint32_t window_width,
                              uint32_t window_height);
uint32_t desktop_app_mouse_sensitivity(void);

#endif
