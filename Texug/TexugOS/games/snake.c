#include <stdint.h>

#include "pit.h"
#include "snake.h"
#include "video.h"

#define SNAKE_COLUMNS 19u
#define SNAKE_ROWS 12u
#define SNAKE_CAPACITY (SNAKE_COLUMNS * SNAKE_ROWS)
#define SNAKE_CELL_SIZE 8u
#define SNAKE_MOVE_TICKS 12u

struct snake_point {
    uint8_t x;
    uint8_t y;
};

static struct snake_point snake_body[SNAKE_CAPACITY];
static uint32_t snake_length;
static struct snake_point snake_food;
static uint32_t snake_score;
static uint32_t snake_last_tick;
static uint32_t random_state;
static int8_t snake_direction_x;
static int8_t snake_direction_y;
static uint8_t snake_game_over;
static uint8_t snake_paused;

static uint32_t next_random(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

static int point_is_snake(uint32_t x, uint32_t y)
{
    for (uint32_t index = 0u; index < snake_length; ++index) {
        if (snake_body[index].x == x && snake_body[index].y == y) {
            return 1;
        }
    }
    return 0;
}

static void place_food(void)
{
    for (uint32_t attempt = 0u; attempt < SNAKE_CAPACITY; ++attempt) {
        uint32_t cell = next_random() % SNAKE_CAPACITY;
        uint32_t x = cell % SNAKE_COLUMNS;
        uint32_t y = cell / SNAKE_COLUMNS;
        if (!point_is_snake(x, y)) {
            snake_food.x = (uint8_t)x;
            snake_food.y = (uint8_t)y;
            return;
        }
    }
    snake_game_over = 1u;
}

static void reset_game(void)
{
    snake_length = 3u;
    snake_body[0] = (struct snake_point){9u, 6u};
    snake_body[1] = (struct snake_point){8u, 6u};
    snake_body[2] = (struct snake_point){7u, 6u};
    snake_direction_x = 1;
    snake_direction_y = 0;
    snake_score = 0u;
    snake_game_over = 0u;
    snake_paused = 0u;
    snake_last_tick = pit_ticks_read();
    place_food();
}

void snake_initialize(void)
{
    random_state = pit_ticks_read() ^ 0x534e414bu;
    reset_game();
}

static void update_game(void)
{
    uint32_t ticks = pit_ticks_read();
    if (snake_game_over || snake_paused ||
        (uint32_t)(ticks - snake_last_tick) < SNAKE_MOVE_TICKS) {
        return;
    }
    snake_last_tick = ticks;

    int32_t next_x = (int32_t)snake_body[0].x + snake_direction_x;
    int32_t next_y = (int32_t)snake_body[0].y + snake_direction_y;
    if (next_x < 0 || next_x >= (int32_t)SNAKE_COLUMNS ||
        next_y < 0 || next_y >= (int32_t)SNAKE_ROWS) {
        snake_game_over = 1u;
        return;
    }

    int ate_food = (uint32_t)next_x == snake_food.x &&
                   (uint32_t)next_y == snake_food.y;
    uint32_t collision_length = ate_food ? snake_length : snake_length - 1u;
    for (uint32_t index = 0u; index < collision_length; ++index) {
        if (snake_body[index].x == (uint32_t)next_x &&
            snake_body[index].y == (uint32_t)next_y) {
            snake_game_over = 1u;
            return;
        }
    }

    if (ate_food && snake_length < SNAKE_CAPACITY) {
        ++snake_length;
    }
    for (uint32_t index = snake_length - 1u; index != 0u; --index) {
        snake_body[index] = snake_body[index - 1u];
    }
    snake_body[0] = (struct snake_point){
        (uint8_t)next_x, (uint8_t)next_y,
    };
    if (ate_food) {
        ++snake_score;
        place_food();
    }
}

void snake_handle_key(char key)
{
    if (key == 'r' || key == 'R' ||
        (key == ' ' && snake_game_over)) {
        reset_game();
        return;
    }
    if (key == 'p' || key == 'P') {
        snake_paused ^= 1u;
        snake_last_tick = pit_ticks_read();
        return;
    }
    if (snake_game_over || snake_paused) {
        return;
    }
    if ((key == 'w' || key == 'W') && snake_direction_y == 0) {
        snake_direction_x = 0;
        snake_direction_y = -1;
    } else if ((key == 's' || key == 'S') && snake_direction_y == 0) {
        snake_direction_x = 0;
        snake_direction_y = 1;
    } else if ((key == 'a' || key == 'A') && snake_direction_x == 0) {
        snake_direction_x = -1;
        snake_direction_y = 0;
    } else if ((key == 'd' || key == 'D') && snake_direction_x == 0) {
        snake_direction_x = 1;
        snake_direction_y = 0;
    }
}

void snake_draw(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    update_game();
    video_draw_text(x + 8, y + 3, "SNAKE", 2u);
    char score_text[16] = "SCORE ";
    uint32_t score = snake_score;
    char digits[10];
    uint32_t count = 0u;
    do {
        digits[count++] = (char)('0' + score % 10u);
        score /= 10u;
    } while (score != 0u && count < sizeof(digits));
    uint32_t output = 6u;
    while (count != 0u && output + 1u < sizeof(score_text)) {
        score_text[output++] = digits[--count];
    }
    score_text[output] = '\0';
    video_draw_text(x + 80, y + 3, score_text, 1u);

    uint32_t board_width = SNAKE_COLUMNS * SNAKE_CELL_SIZE;
    uint32_t board_height = SNAKE_ROWS * SNAKE_CELL_SIZE;
    int32_t board_x = x + 7;
    int32_t board_y = y + 17;
    if (board_width + 14u > width || board_height + 22u > height) {
        return;
    }
    video_fill_rect(board_x - 2, board_y - 2, board_width + 4u,
                    board_height + 4u, 8u);
    video_fill_rect(board_x, board_y, board_width, board_height, 0u);
    video_fill_rect(board_x + (int32_t)snake_food.x * SNAKE_CELL_SIZE + 1,
                    board_y + (int32_t)snake_food.y * SNAKE_CELL_SIZE + 1,
                    SNAKE_CELL_SIZE - 2u, SNAKE_CELL_SIZE - 2u, 12u);
    for (uint32_t index = 0u; index < snake_length; ++index) {
        uint8_t color = index == 0u ? 10u : 2u;
        video_fill_rect(
            board_x + (int32_t)snake_body[index].x * SNAKE_CELL_SIZE + 1,
            board_y + (int32_t)snake_body[index].y * SNAKE_CELL_SIZE + 1,
            SNAKE_CELL_SIZE - 2u, SNAKE_CELL_SIZE - 2u, color);
    }
    if (snake_game_over) {
        video_draw_text(board_x + 25, board_y + 46, "GAME OVER", 12u);
        video_draw_text(board_x + 25, board_y + 57, "R TO RESTART", 15u);
    } else if (snake_paused) {
        video_draw_text(board_x + 49, board_y + 46, "PAUSED", 14u);
    }
}
