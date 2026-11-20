#include <stdint.h>

#include "paint.h"
#include "video.h"

#define PAINT_COLUMNS 100u
#define PAINT_ROWS 40u
#define PAINT_PIXEL_SIZE 2u
#define PAINT_CANVAS_X 4
#define PAINT_CANVAS_Y 16
#define PAINT_PALETTE_Y 2
#define PAINT_SWATCH_STEP 13
#define PAINT_CLEAR_X 214

static uint8_t canvas[PAINT_ROWS][PAINT_COLUMNS];
static uint8_t selected_color;
static uint8_t pointer_was_drawing;
static int32_t previous_x;
static int32_t previous_y;

static void draw_pixel(int32_t x, int32_t y)
{
    if (x >= 0 && x < (int32_t)PAINT_COLUMNS &&
        y >= 0 && y < (int32_t)PAINT_ROWS) {
        canvas[y][x] = selected_color;
    }
}

void paint_initialize(void)
{
    selected_color = 0u;
    pointer_was_drawing = 0u;
    previous_x = 0;
    previous_y = 0;
    for (uint32_t y = 0u; y < PAINT_ROWS; ++y) {
        for (uint32_t x = 0u; x < PAINT_COLUMNS; ++x) {
            canvas[y][x] = 15u;
        }
    }
}

void paint_draw(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    video_draw_text(x + 4, y + 3, "COLOR", 1u);
    for (uint32_t color = 0u; color < 16u; ++color) {
        int32_t swatch_x = x + 4 + (int32_t)color * PAINT_SWATCH_STEP;
        video_fill_rect(swatch_x, y + PAINT_PALETTE_Y, 10u, 8u,
                        (uint8_t)color);
        if (color == selected_color) {
            video_fill_rect(swatch_x - 1, y + PAINT_PALETTE_Y - 1, 12u, 1u,
                            0u);
            video_fill_rect(swatch_x - 1, y + PAINT_PALETTE_Y + 8, 12u, 1u,
                            0u);
        }
    }
    video_draw_button(x + PAINT_CLEAR_X, y + 1, 24u, 11u, "CLR", 4u, 15u);

    uint32_t canvas_width = PAINT_COLUMNS * PAINT_PIXEL_SIZE;
    uint32_t canvas_height = PAINT_ROWS * PAINT_PIXEL_SIZE;
    if (canvas_width + PAINT_CANVAS_X * 2u > width ||
        canvas_height + PAINT_CANVAS_Y + 2u > height) {
        return;
    }
    video_fill_rect(x + PAINT_CANVAS_X - 1, y + PAINT_CANVAS_Y - 1,
                    canvas_width + 2u, canvas_height + 2u, 8u);
    for (uint32_t row = 0u; row < PAINT_ROWS; ++row) {
        uint32_t column = 0u;
        while (column < PAINT_COLUMNS) {
            uint8_t color = canvas[row][column];
            uint32_t end = column + 1u;
            while (end < PAINT_COLUMNS && canvas[row][end] == color) {
                ++end;
            }
            video_fill_rect(
                x + PAINT_CANVAS_X + (int32_t)column * PAINT_PIXEL_SIZE,
                y + PAINT_CANVAS_Y + (int32_t)row * PAINT_PIXEL_SIZE,
                (end - column) * PAINT_PIXEL_SIZE, PAINT_PIXEL_SIZE, color);
            column = end;
        }
    }
}

void paint_handle_pointer(int32_t x, int32_t y, uint32_t buttons)
{
    if ((buttons & 1u) == 0u) {
        pointer_was_drawing = 0u;
        return;
    }
    int32_t pixel_x = (x - PAINT_CANVAS_X) / (int32_t)PAINT_PIXEL_SIZE;
    int32_t pixel_y = (y - PAINT_CANVAS_Y) / (int32_t)PAINT_PIXEL_SIZE;
    if (x < PAINT_CANVAS_X ||
        x >= PAINT_CANVAS_X + (int32_t)(PAINT_COLUMNS * PAINT_PIXEL_SIZE) ||
        y < PAINT_CANVAS_Y ||
        y >= PAINT_CANVAS_Y + (int32_t)(PAINT_ROWS * PAINT_PIXEL_SIZE)) {
        pointer_was_drawing = 0u;
        return;
    }
    if (!pointer_was_drawing) {
        draw_pixel(pixel_x, pixel_y);
    } else {
        int32_t x0 = previous_x;
        int32_t y0 = previous_y;
        int32_t dx = x0 < pixel_x ? pixel_x - x0 : x0 - pixel_x;
        int32_t sx = x0 < pixel_x ? 1 : -1;
        int32_t dy = y0 < pixel_y ? pixel_y - y0 : y0 - pixel_y;
        int32_t sy = y0 < pixel_y ? 1 : -1;
        int32_t error = dx - dy;
        for (;;) {
            draw_pixel(x0, y0);
            if (x0 == pixel_x && y0 == pixel_y) {
                break;
            }
            int32_t twice_error = error * 2;
            if (twice_error > -dy) {
                error -= dy;
                x0 += sx;
            }
            if (twice_error < dx) {
                error += dx;
                y0 += sy;
            }
        }
    }
    previous_x = pixel_x;
    previous_y = pixel_y;
    pointer_was_drawing = 1u;
}

void paint_handle_click(int32_t x, int32_t y,
                        uint32_t width, uint32_t height)
{
    (void)height;
    if (y >= 1 && y < 12 && x >= PAINT_CLEAR_X &&
        x < PAINT_CLEAR_X + 24) {
        for (uint32_t row = 0u; row < PAINT_ROWS; ++row) {
            for (uint32_t column = 0u; column < PAINT_COLUMNS; ++column) {
                canvas[row][column] = 15u;
            }
        }
        pointer_was_drawing = 0u;
        return;
    }
    for (uint32_t color = 0u; color < 16u; ++color) {
        int32_t swatch_x = 4 + (int32_t)color * PAINT_SWATCH_STEP;
        if (x >= swatch_x && x < swatch_x + 10 &&
            y >= PAINT_PALETTE_Y && y < PAINT_PALETTE_Y + 8) {
            selected_color = (uint8_t)color;
            return;
        }
    }
    if (x < 0 || y < 0 || (uint32_t)x >= width) {
        pointer_was_drawing = 0u;
    }
}
