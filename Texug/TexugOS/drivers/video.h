#ifndef TEXUGOS_VIDEO_H
#define TEXUGOS_VIDEO_H

#include <stdint.h>

#define VIDEO_WIDTH 320u
#define VIDEO_HEIGHT 200u
#define VIDEO_VBE_WIDTH 640u
#define VIDEO_VBE_HEIGHT 480u

int video_set_graphics_mode(void);
int video_set_text_mode(void);
uint32_t video_display_width(void);
uint32_t video_display_height(void);
void video_clear(uint8_t color);
void video_present(void);
void video_fill_rect(int32_t x, int32_t y, uint32_t width, uint32_t height,
                     uint8_t color);
void video_fill_circle(int32_t center_x, int32_t center_y, uint32_t radius,
                       uint8_t color);
void video_draw_mouse_cursor(int32_t x, int32_t y);
void video_draw_text(int32_t x, int32_t y, const char *text, uint8_t color);
void video_draw_button(int32_t x, int32_t y, uint32_t width, uint32_t height,
                       const char *label, uint8_t background,
                       uint8_t foreground);

#endif
