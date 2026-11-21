#include <stdint.h>

#include "apps.h"
#include "console.h"
#include "desktop.h"
#include "keyboard.h"
#include "mouse.h"
#include "pit.h"
#include "video.h"

#define WINDOW_COUNT 8u
#define TASKBAR_Y 184
#define WINDOW_MIN_WIDTH 112u
#define WINDOW_MIN_HEIGHT 72u
#define WINDOW_DEFAULT_WIDTH 248u
#define WINDOW_DEFAULT_HEIGHT 138u
#define DESKTOP_RENDER_INTERVAL_TICKS 2u
#define TASK_BUTTON_X 84
#define TASK_BUTTON_PITCH 24u
#define TASK_BUTTON_WIDTH 23u

struct desktop_window {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint8_t open;
    uint8_t minimized;
    uint8_t maximized;
};

enum drag_mode {
    DRAG_NONE,
    DRAG_TITLE,
    DRAG_RESIZE
};

static const char *const window_titles[WINDOW_COUNT] = {
    "Files", "Settings", "Browser", "Arena", "Notepad", "Snake", "Tetris",
    "Paint",
};
static const char *const task_titles[WINDOW_COUNT] = {
    "File", "Set", "Web", "Game", "Note", "Snake", "Tetris", "Paint",
};
static struct desktop_window windows[WINDOW_COUNT];
static int32_t active_window = -1;
static uint8_t menu_open;
static uint8_t return_to_shell_requested;
static enum drag_mode dragging;
static int32_t drag_offset_x;
static int32_t drag_offset_y;

static int32_t clamp(int32_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static void draw_icon(uint32_t index)
{
    int32_t tile_x = 8 + (int32_t)(index % 2u) * 72;
    int32_t y = 12 + (int32_t)(index / 2u) * 40;
    int32_t icon_x = tile_x + 18;
    if (index == 0u) {
        video_fill_rect(icon_x + 4, y + 7, 13u, 5u, 14u);
        video_fill_rect(icon_x + 2, y + 11, 28u, 14u, 6u);
        video_fill_rect(icon_x + 8, y + 8, 17u, 15u, 15u);
        video_fill_rect(icon_x + 10, y + 11, 12u, 1u, 9u);
        video_fill_rect(icon_x + 10, y + 14, 10u, 1u, 9u);
        video_fill_rect(icon_x + 2, y + 18, 28u, 7u, 14u);
        video_fill_rect(icon_x + 3, y + 18, 26u, 1u, 15u);
    } else if (index == 1u) {
        video_fill_rect(icon_x + 13, y + 3, 6u, 5u, 7u);
        video_fill_rect(icon_x + 13, y + 21, 6u, 5u, 7u);
        video_fill_rect(icon_x + 3, y + 12, 5u, 6u, 7u);
        video_fill_rect(icon_x + 24, y + 12, 5u, 6u, 7u);
        video_fill_circle(icon_x + 16, y + 15, 12u, 0u);
        video_fill_circle(icon_x + 16, y + 15, 9u, 7u);
        video_fill_rect(icon_x + 15, y + 5, 3u, 20u, 7u);
        video_fill_rect(icon_x + 6, y + 14, 20u, 3u, 7u);
        video_fill_circle(icon_x + 16, y + 15, 4u, 0u);
        video_fill_circle(icon_x + 16, y + 15, 2u, 11u);
    } else if (index == 2u) {
        video_fill_circle(icon_x + 16, y + 15, 13u, 0u);
        video_fill_circle(icon_x + 16, y + 15, 11u, 9u);
        video_fill_rect(icon_x + 6, y + 10, 20u, 1u, 15u);
        video_fill_rect(icon_x + 5, y + 14, 22u, 2u, 15u);
        video_fill_rect(icon_x + 7, y + 19, 18u, 1u, 15u);
        video_fill_rect(icon_x + 15, y + 5, 2u, 20u, 15u);
        video_fill_rect(icon_x + 10, y + 7, 2u, 4u, 10u);
        video_fill_rect(icon_x + 21, y + 18, 3u, 4u, 10u);
    } else if (index == 3u) {
        video_fill_circle(icon_x + 8, y + 16, 8u, 0u);
        video_fill_circle(icon_x + 24, y + 16, 8u, 0u);
        video_fill_rect(icon_x + 8, y + 8, 16u, 17u, 0u);
        video_fill_circle(icon_x + 8, y + 16, 6u, 7u);
        video_fill_circle(icon_x + 24, y + 16, 6u, 7u);
        video_fill_rect(icon_x + 8, y + 10, 16u, 13u, 7u);
        video_fill_rect(icon_x + 5, y + 15, 7u, 2u, 15u);
        video_fill_rect(icon_x + 8, y + 12, 2u, 8u, 15u);
        video_fill_circle(icon_x + 23, y + 14, 2u, 4u);
        video_fill_circle(icon_x + 27, y + 18, 2u, 14u);
    } else if (index == 4u) {
        video_fill_rect(icon_x + 6, y + 3, 21u, 24u, 0u);
        video_fill_rect(icon_x + 8, y + 4, 17u, 21u, 15u);
        video_fill_rect(icon_x + 11, y + 8, 11u, 1u, 9u);
        video_fill_rect(icon_x + 11, y + 12, 11u, 1u, 9u);
        video_fill_rect(icon_x + 11, y + 16, 8u, 1u, 9u);
        video_fill_rect(icon_x + 20, y + 18, 8u, 3u, 4u);
        video_fill_rect(icon_x + 24, y + 14, 3u, 8u, 4u);
    } else if (index == 5u) {
        video_fill_rect(icon_x + 5, y + 5, 23u, 18u, 0u);
        video_fill_rect(icon_x + 8, y + 8, 6u, 6u, 10u);
        video_fill_rect(icon_x + 14, y + 8, 6u, 6u, 2u);
        video_fill_rect(icon_x + 20, y + 8, 6u, 6u, 2u);
        video_fill_rect(icon_x + 20, y + 15, 5u, 5u, 12u);
    } else if (index == 6u) {
        video_fill_rect(icon_x + 7, y + 4, 20u, 22u, 0u);
        video_fill_rect(icon_x + 8, y + 5, 6u, 6u, 11u);
        video_fill_rect(icon_x + 14, y + 5, 6u, 6u, 11u);
        video_fill_rect(icon_x + 14, y + 11, 6u, 6u, 14u);
        video_fill_rect(icon_x + 20, y + 11, 6u, 6u, 14u);
        video_fill_rect(icon_x + 8, y + 17, 6u, 6u, 9u);
        video_fill_rect(icon_x + 14, y + 17, 6u, 6u, 9u);
    } else {
        video_fill_rect(icon_x + 9, y + 4, 15u, 19u, 0u);
        video_fill_rect(icon_x + 11, y + 7, 11u, 13u, 15u);
        video_fill_rect(icon_x + 15, y + 3, 4u, 5u, 4u);
        video_fill_rect(icon_x + 8, y + 21, 17u, 3u, 8u);
    }

    video_draw_text_centered(tile_x, y + 31, 68u, 11u,
                             window_titles[index], 15u);
}

/* Botao do painel: cinza-azulado normal, azul quando ativo. */
static void draw_panel_button(int32_t x, int32_t y, uint32_t width,
                              uint32_t height, const char *label,
                              int active)
{
    video_draw_panel(x, y, width, height, active ? 9u : 2u,
                     active ? 11u : 8u, 2u);
    video_draw_text_centered(x, y, width, height, label, 15u);
}

static void draw_wallpaper(void)
{
    /* Fundo liso, uma unica cor. */
    video_fill_rect(0, 0, VIDEO_WIDTH, (uint32_t)TASKBAR_Y, 3u);

    for (uint32_t index = 0; index < WINDOW_COUNT; ++index) {
        draw_icon(index);
    }

    /* Painel inferior. */
    video_fill_rect(0, TASKBAR_Y, VIDEO_WIDTH, VIDEO_HEIGHT - TASKBAR_Y, 1u);
    video_draw_hline(0, TASKBAR_Y, VIDEO_WIDTH, 8u);
    draw_panel_button(5, TASKBAR_Y + 2, 34u, 14u, "Menu", menu_open);
    draw_panel_button(42, TASKBAR_Y + 2, 38u, 14u, "Shell", 0);

    for (uint32_t index = 0; index < WINDOW_COUNT; ++index) {
        if (windows[index].open && !windows[index].minimized) {
            int32_t button_x = TASK_BUTTON_X +
                               (int32_t)(index * TASK_BUTTON_PITCH);
            draw_panel_button(button_x, TASKBAR_Y + 2, TASK_BUTTON_WIDTH, 14u,
                              task_titles[index],
                              (int32_t)index == active_window);
        }
    }

    uint32_t seconds = pit_ticks_read() / 100u;
    char clock_text[6] = {
        (char)('0' + (seconds / 60u) % 10u),
        (char)('0' + (seconds / 10u) % 6u),
        ':',
        (char)('0' + (seconds % 60u) / 10u),
        (char)('0' + seconds % 10u),
        '\0',
    };
    video_draw_text_middle(316 - (int32_t)video_text_width(clock_text),
                           TASKBAR_Y, VIDEO_HEIGHT - TASKBAR_Y,
                           clock_text, 15u);
}

static void draw_window(uint32_t index)
{
    struct desktop_window *window = &windows[index];
    if (!window->open || window->minimized) {
        return;
    }

    int32_t x = window->x;
    int32_t y = window->y;
    uint32_t width = window->width;
    uint32_t height = window->height;
    int active = (int32_t)index == active_window;
    uint8_t frame = active ? 9u : 7u;   /* barra de titulo e moldura */
    uint8_t edge = active ? 2u : 8u;    /* borda de 1 pixel */
    uint8_t ink = active ? 15u : 1u;    /* texto do titulo */

    /* Moldura lisa e sem sombra, com borda fina e cantos suaves. */
    video_draw_panel(x, y, width, height, frame, edge, 3u);
    video_draw_text_middle(x + 7, y + 1, 14u, window_titles[index], ink);

    /* Botoes: minimizar, maximizar, fechar. */
    int32_t minimize_x = x + (int32_t)width - 36;
    int32_t maximize_x = x + (int32_t)width - 24;
    int32_t close_x = x + (int32_t)width - 12;
    video_draw_panel(minimize_x, y + 3, 10u, 9u, 7u, edge, 2u);
    video_fill_rect(minimize_x + 2, y + 8, 6u, 1u, 1u);
    video_draw_panel(maximize_x, y + 3, 10u, 9u, 7u, edge, 2u);
    video_draw_frame(maximize_x + 2, y + 5, 6u, 5u, 1u);
    video_draw_panel(close_x, y + 3, 10u, 9u, 4u, edge, 2u);
    video_draw_cross(close_x, y + 3, 10u, 9u, 15u);

    /* Area do aplicativo, com borda propria. */
    video_fill_rect(x + 3, y + 17, width - 6u, height - 20u, 15u);
    desktop_app_draw(index, x + 3, y + 17, width - 6u, height - 20u);
    video_draw_frame(x + 3, y + 17, width - 6u, height - 20u, edge);

    /* Alca de redimensionar: tres pontos na moldura. */
    for (int32_t dot = 0; dot < 3; ++dot) {
        video_fill_rect(x + (int32_t)width - 9 + dot * 2,
                        y + (int32_t)height - 2, 1u, 1u, ink);
    }
}

static void draw_start_menu(void)
{
    if (!menu_open) {
        return;
    }
    video_draw_panel(4, 48, 136u, 134u, 15u, 8u, 3u);
    video_draw_text_middle(12, 50, 14u, "TexugOS", 9u);
    video_draw_hline(8, 64, 124u, 7u);
    for (uint32_t index = 0u; index < WINDOW_COUNT; ++index) {
        uint32_t column = index % 2u;
        uint32_t row = index / 2u;
        video_draw_button(8 + (int32_t)column * 64,
                          66 + (int32_t)row * 22, 60u, 18u,
                          window_titles[index], 2u, 15u);
    }
    video_draw_button(8, 164, 124u, 14u, "Return to shell", 9u, 15u);
}

static void draw_cursor(uint32_t x, uint32_t y)
{
    video_draw_mouse_cursor((int32_t)x, (int32_t)y);
}

static void set_default_window(uint32_t index,
                               struct desktop_window *window)
{
    window->x = 32 + (int32_t)(index % 5u) * 8;
    window->y = 28 + (int32_t)(index % 5u) * 7;
    window->width = WINDOW_DEFAULT_WIDTH;
    window->height = WINDOW_DEFAULT_HEIGHT;
    if (window->y + (int32_t)window->height > TASKBAR_Y) {
        window->y = TASKBAR_Y - (int32_t)window->height;
    }
}

static void draw_desktop(uint32_t mouse_x, uint32_t mouse_y)
{
    draw_wallpaper();
    for (uint32_t index = 0; index < WINDOW_COUNT; ++index) {
        if ((int32_t)index != active_window) {
            draw_window(index);
        }
    }
    if (active_window >= 0) {
        draw_window((uint32_t)active_window);
    }
    draw_start_menu();
    draw_cursor(mouse_x, mouse_y);
}

static void open_window(uint32_t index)
{
    if (index >= WINDOW_COUNT) {
        return;
    }
    struct desktop_window *window = &windows[index];
    window->open = 1u;
    window->minimized = 0u;
    if (!window->maximized) {
        set_default_window(index, window);
    }
    active_window = (int32_t)index;
    menu_open = 0u;
}

static int window_contains_point(const struct desktop_window *window,
                                 uint32_t x, uint32_t y)
{
    return window->open && !window->minimized &&
           (int32_t)x >= window->x && (int32_t)y >= window->y &&
           (int32_t)x < window->x + (int32_t)window->width &&
           (int32_t)y < window->y + (int32_t)window->height;
}

static int32_t hit_test_window(uint32_t x, uint32_t y)
{
    if (active_window >= 0 &&
        window_contains_point(&windows[(uint32_t)active_window], x, y)) {
        return active_window;
    }

    for (uint32_t index = WINDOW_COUNT; index != 0u; --index) {
        uint32_t candidate = index - 1u;
        if ((int32_t)candidate != active_window &&
            window_contains_point(&windows[candidate], x, y)) {
            return (int32_t)candidate;
        }
    }
    return -1;
}

static void handle_click(uint32_t x, uint32_t y)
{
    if (y >= (uint32_t)TASKBAR_Y && x >= 5u && x < 40u) {
        menu_open ^= 1u;
        return;
    }
    if (y >= (uint32_t)TASKBAR_Y && x >= 42u && x < 81u) {
        return_to_shell_requested = 1u;
        return;
    }
    if (menu_open) {
        if (x >= 8u && x < 132u) {
            if (y >= 66u && y < 154u) {
                uint32_t column = x < 70u ? 0u : 1u;
                uint32_t row = (y - 66u) / 22u;
                open_window(row * 2u + column);
            } else if (y >= 164u && y < 180u) {
                return_to_shell_requested = 1u;
            }
        }
        menu_open = 0u;
        return;
    }

    int32_t hit_window = hit_test_window(x, y);
    if (hit_window >= 0) {
        uint32_t index = (uint32_t)hit_window;
        struct desktop_window *window = &windows[index];
        active_window = (int32_t)index;
        int32_t local_x = (int32_t)x - window->x;
        int32_t local_y = (int32_t)y - window->y;
        if (local_y < 15 && local_x >= (int32_t)window->width - 14) {
            window->open = 0u;
            active_window = -1;
        } else if (local_y < 15 && local_x >= (int32_t)window->width - 25) {
            window->maximized ^= 1u;
            if (window->maximized) {
                window->x = 0;
                window->y = 15;
                window->width = VIDEO_WIDTH;
                window->height = 166u;
            } else {
                set_default_window(index, window);
            }
        } else if (local_y < 15 &&
                   local_x >= (int32_t)window->width - 36) {
            window->minimized = 1u;
            active_window = -1;
        } else if (local_y < 15) {
            dragging = DRAG_TITLE;
            drag_offset_x = local_x;
            drag_offset_y = local_y;
        } else if (local_x >= (int32_t)window->width - 10 &&
                   local_y >= (int32_t)window->height - 10) {
            dragging = DRAG_RESIZE;
            drag_offset_x = (int32_t)x;
            drag_offset_y = (int32_t)y;
        } else {
            desktop_app_handle_click(index, local_x, local_y,
                                     window->width, window->height);
        }
        return;
    }

    if (x >= 8u && x < 148u && y >= 12u && y < 172u) {
        uint32_t column = x < 76u ? 0u : 1u;
        uint32_t row = (y - 12u) / 40u;
        uint32_t index = row * 2u + column;
        if (index < WINDOW_COUNT) {
            open_window(index);
            return;
        }
    }

    if (y >= (uint32_t)TASKBAR_Y) {
        uint32_t index =
            x >= (uint32_t)TASK_BUTTON_X &&
            x < (uint32_t)TASK_BUTTON_X + WINDOW_COUNT * TASK_BUTTON_PITCH
                ? (x - (uint32_t)TASK_BUTTON_X) / TASK_BUTTON_PITCH
                : WINDOW_COUNT;
        if (index < WINDOW_COUNT && windows[index].open) {
            windows[index].minimized = 0u;
            active_window = (int32_t)index;
        }
    }
}

static void update_drag(uint32_t x, uint32_t y, uint32_t buttons)
{
    if ((buttons & 1u) == 0u) {
        dragging = DRAG_NONE;
        return;
    }
    if (active_window < 0 || dragging == DRAG_NONE) {
        return;
    }

    struct desktop_window *window = &windows[(uint32_t)active_window];
    if (dragging == DRAG_TITLE) {
        window->x = clamp((int32_t)x - drag_offset_x,
                          0, (int32_t)VIDEO_WIDTH - (int32_t)window->width);
        window->y = clamp((int32_t)y - drag_offset_y,
                          15, TASKBAR_Y - (int32_t)window->height);
    } else {
        window->width = (uint32_t)clamp((int32_t)x - window->x + 1,
                                        (int32_t)WINDOW_MIN_WIDTH,
                                        (int32_t)VIDEO_WIDTH - window->x);
        window->height = (uint32_t)clamp((int32_t)y - window->y + 1,
                                         (int32_t)WINDOW_MIN_HEIGHT,
                                         TASKBAR_Y - window->y);
    }
}

static void exit_to_shell(void)
{
    (void)video_set_text_mode();
    console_clear();
    console_write("Returned from TexugOS Desktop.\n");
}

void desktop_run(void)
{
    for (uint32_t index = 0; index < WINDOW_COUNT; ++index) {
        windows[index] = (struct desktop_window){0};
    }
    active_window = -1;
    menu_open = 0u;
    return_to_shell_requested = 0u;
    dragging = DRAG_NONE;
    desktop_apps_initialize();
    if (!video_set_graphics_mode()) {
        console_write("Unable to set VGA graphics mode.\n");
        return;
    }

    uint8_t previous_buttons = 0u;
    uint32_t previous_mouse_x = VIDEO_WIDTH / 2u;
    uint32_t last_render_tick =
        pit_ticks_read() - DESKTOP_RENDER_INTERVAL_TICKS;
    for (;;) {
        struct mouse_state mouse;
        mouse_get_state(&mouse);
        uint32_t mouse_x =
            mouse.x * desktop_app_mouse_sensitivity() / 2u;
        uint32_t mouse_y = mouse.y;
        if (mouse_x >= VIDEO_WIDTH) {
            mouse_x = VIDEO_WIDTH - 1u;
        }
        if (mouse_y >= VIDEO_HEIGHT) {
            mouse_y = VIDEO_HEIGHT - 1u;
        }
        char key;
        while (keyboard_read_character(&key)) {
            if ((uint8_t)key == 0x1bu) {
                exit_to_shell();
                return;
            }
            if (active_window >= 0 &&
                windows[(uint32_t)active_window].open &&
                !windows[(uint32_t)active_window].minimized) {
                desktop_app_handle_key((uint32_t)active_window, key);
            } else if (key == '1') {
                open_window(0u);
            } else if (key == '2') {
                open_window(1u);
            } else if (key == '3') {
                open_window(2u);
            } else if (key == '4') {
                open_window(3u);
            } else if (key == '5') {
                open_window(4u);
            } else if (key == '6') {
                open_window(5u);
            } else if (key == '7') {
                open_window(6u);
            } else if (key == '8') {
                open_window(7u);
            } else if (key == '0') {
                return_to_shell_requested = 1u;
            }
        }
        if (return_to_shell_requested != 0u) {
            exit_to_shell();
            return;
        }

        if ((mouse.buttons & 1u) != 0u &&
            (previous_buttons & 1u) == 0u) {
            handle_click(mouse_x, mouse_y);
        }
        update_drag(mouse_x, mouse_y, mouse.buttons);
        if (return_to_shell_requested != 0u) {
            exit_to_shell();
            return;
        }
        int32_t mouse_delta_x = (int32_t)mouse_x - (int32_t)previous_mouse_x;
        if (active_window == 3) {
            desktop_app_handle_mouse(3u, mouse_delta_x);
        }
        if (active_window >= 0 &&
            windows[(uint32_t)active_window].open &&
            !windows[(uint32_t)active_window].minimized) {
            struct desktop_window *window =
                &windows[(uint32_t)active_window];
            desktop_app_handle_pointer(
                (uint32_t)active_window,
                (int32_t)mouse_x - window->x - 3,
                (int32_t)mouse_y - window->y - 17,
                mouse.buttons);
        }
        previous_mouse_x = mouse_x;
        previous_buttons = mouse.buttons;
        uint32_t ticks = pit_ticks_read();
        if ((uint32_t)(ticks - last_render_tick) >=
            DESKTOP_RENDER_INTERVAL_TICKS) {
            draw_desktop(mouse_x, mouse_y);
            video_present();
            last_render_tick = ticks;
        }
        __asm__ volatile ("hlt");
    }
}