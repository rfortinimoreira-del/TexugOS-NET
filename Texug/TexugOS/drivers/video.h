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

/* Primitivas lisas com borda de 1 pixel real (nitidas em qualquer modo). */
void video_draw_frame(int32_t x, int32_t y, uint32_t width, uint32_t height,
                      uint8_t color);
void video_draw_hline(int32_t x, int32_t y, uint32_t width, uint8_t color);
void video_draw_cross(int32_t x, int32_t y, uint32_t width, uint32_t height,
                      uint8_t color);
void video_draw_panel(int32_t x, int32_t y, uint32_t width, uint32_t height,
                      uint8_t fill, uint8_t border, uint32_t radius);
void video_draw_button(int32_t x, int32_t y, uint32_t width, uint32_t height,
                       const char *label, uint8_t background,
                       uint8_t foreground);

/* Texto em DejaVu Sans (proporcional). Larguras em unidades logicas. */
uint32_t video_text_width(const char *text);
void video_draw_text(int32_t x, int32_t y, const char *text, uint8_t color);
void video_draw_text_middle(int32_t x, int32_t y, uint32_t height,
                            const char *text, uint8_t color);
void video_draw_text_centered(int32_t x, int32_t y, uint32_t width,
                              uint32_t height, const char *text,
                              uint8_t color);
void video_draw_char_cell(int32_t x, int32_t y, char character,
                          uint8_t color);

#endif