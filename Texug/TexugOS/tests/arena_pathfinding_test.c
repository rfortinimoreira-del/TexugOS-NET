#include "../games/arena.c"

uint32_t pit_ticks_read(void)
{
    return 0u;
}

void pit_beep(uint32_t frequency_hz, uint32_t duration_ticks)
{
    (void)frequency_hz;
    (void)duration_ticks;
}

void video_fill_rect(int32_t x, int32_t y, uint32_t width, uint32_t height,
                     uint8_t color)
{
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)color;
}

void video_draw_text(int32_t x, int32_t y, const char *text, uint8_t color)
{
    (void)x;
    (void)y;
    (void)text;
    (void)color;
}

int main(void)
{
    arena_initialize();
    arena_handle_key('\n');

    player_x = 2 * 256 + 128;
    player_y = 2 * 256 + 128;
    enemies[0] = (struct arena_enemy){4u, 2u, 3u, 1u};
    enemies[1].alive = 0u;

    move_enemy_toward_player(0u);
    return enemies[0].x == 4u && enemies[0].y == 3u ? 0 : 1;
}
