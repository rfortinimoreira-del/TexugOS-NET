#include <stdint.h>

#include "pit.h"
#include "tetris.h"
#include "video.h"

#define TETRIS_COLUMNS 10u
#define TETRIS_ROWS 18u
#define TETRIS_CELL_SIZE 5u
#define TETRIS_FALL_TICKS 38u

static const uint8_t piece_rows[7][4] = {
    {0u, 15u, 0u, 0u},
    {6u, 6u, 0u, 0u},
    {2u, 7u, 0u, 0u},
    {6u, 3u, 0u, 0u},
    {3u, 6u, 0u, 0u},
    {1u, 7u, 0u, 0u},
    {4u, 7u, 0u, 0u},
};
static uint8_t board[TETRIS_ROWS][TETRIS_COLUMNS];
static uint32_t random_state;
static uint32_t score;
static uint32_t lines_cleared;
static uint32_t last_fall_tick;
static int32_t piece_x;
static int32_t piece_y;
static uint8_t piece_type;
static uint8_t piece_rotation;
static uint8_t game_over;
static uint8_t paused;

static uint32_t next_random(void)
{
    random_state = random_state * 1103515245u + 12345u;
    return random_state;
}

static int piece_cell(uint8_t type, uint8_t rotation,
                      uint32_t x, uint32_t y)
{
    for (uint32_t source_y = 0u; source_y < 4u; ++source_y) {
        for (uint32_t source_x = 0u; source_x < 4u; ++source_x) {
            if ((piece_rows[type][source_y] & (1u << source_x)) == 0u) {
                continue;
            }
            uint32_t rotated_x = source_x;
            uint32_t rotated_y = source_y;
            for (uint8_t turn = 0u; turn < rotation; ++turn) {
                uint32_t next_x = 3u - rotated_y;
                rotated_y = rotated_x;
                rotated_x = next_x;
            }
            if (rotated_x == x && rotated_y == y) {
                return 1;
            }
        }
    }
    return 0;
}

static int can_place(int32_t x, int32_t y, uint8_t rotation)
{
    for (uint32_t local_y = 0u; local_y < 4u; ++local_y) {
        for (uint32_t local_x = 0u; local_x < 4u; ++local_x) {
            if (!piece_cell(piece_type, rotation, local_x, local_y)) {
                continue;
            }
            int32_t board_x = x + (int32_t)local_x;
            int32_t board_y = y + (int32_t)local_y;
            if (board_x < 0 || board_x >= (int32_t)TETRIS_COLUMNS ||
                board_y >= (int32_t)TETRIS_ROWS) {
                return 0;
            }
            if (board_y >= 0 &&
                board[board_y][board_x] != 0u) {
                return 0;
            }
        }
    }
    return 1;
}

static void spawn_piece(void)
{
    piece_type = (uint8_t)(next_random() % 7u);
    piece_rotation = 0u;
    piece_x = (int32_t)(TETRIS_COLUMNS / 2u) - 2;
    piece_y = 0;
    if (!can_place(piece_x, piece_y, piece_rotation)) {
        game_over = 1u;
    }
}

static void reset_game(void)
{
    for (uint32_t y = 0u; y < TETRIS_ROWS; ++y) {
        for (uint32_t x = 0u; x < TETRIS_COLUMNS; ++x) {
            board[y][x] = 0u;
        }
    }
    score = 0u;
    lines_cleared = 0u;
    game_over = 0u;
    paused = 0u;
    last_fall_tick = pit_ticks_read();
    spawn_piece();
}

void tetris_initialize(void)
{
    random_state = pit_ticks_read() ^ 0x54455452u;
    reset_game();
}

static void lock_piece(void)
{
    for (uint32_t local_y = 0u; local_y < 4u; ++local_y) {
        for (uint32_t local_x = 0u; local_x < 4u; ++local_x) {
            if (!piece_cell(piece_type, piece_rotation, local_x, local_y)) {
                continue;
            }
            int32_t x = piece_x + (int32_t)local_x;
            int32_t y = piece_y + (int32_t)local_y;
            if (y < 0) {
                game_over = 1u;
                return;
            }
            board[y][x] = (uint8_t)(piece_type + 1u);
        }
    }

    for (int32_t row = (int32_t)TETRIS_ROWS - 1; row >= 0; --row) {
        uint32_t filled = 0u;
        for (uint32_t x = 0u; x < TETRIS_COLUMNS; ++x) {
            filled += board[row][x] != 0u;
        }
        if (filled != TETRIS_COLUMNS) {
            continue;
        }
        for (int32_t destination = row; destination > 0; --destination) {
            for (uint32_t x = 0u; x < TETRIS_COLUMNS; ++x) {
                board[destination][x] = board[destination - 1][x];
            }
        }
        for (uint32_t x = 0u; x < TETRIS_COLUMNS; ++x) {
            board[0][x] = 0u;
        }
        ++lines_cleared;
        score += 100u;
        ++row;
    }
    spawn_piece();
}

static void move_down(void)
{
    if (can_place(piece_x, piece_y + 1, piece_rotation)) {
        ++piece_y;
    } else {
        lock_piece();
    }
}

static void update_game(void)
{
    uint32_t ticks = pit_ticks_read();
    if (game_over || paused ||
        (uint32_t)(ticks - last_fall_tick) < TETRIS_FALL_TICKS) {
        return;
    }
    last_fall_tick = ticks;
    move_down();
}

void tetris_handle_key(char key)
{
    if (key == 'r' || key == 'R' || (key == ' ' && game_over)) {
        reset_game();
        return;
    }
    if (key == 'p' || key == 'P') {
        paused ^= 1u;
        last_fall_tick = pit_ticks_read();
        return;
    }
    if (game_over || paused) {
        return;
    }
    if ((key == 'a' || key == 'A') &&
        can_place(piece_x - 1, piece_y, piece_rotation)) {
        --piece_x;
    } else if ((key == 'd' || key == 'D') &&
               can_place(piece_x + 1, piece_y, piece_rotation)) {
        ++piece_x;
    } else if ((key == 's' || key == 'S')) {
        move_down();
        last_fall_tick = pit_ticks_read();
    } else if (key == 'w' || key == 'W') {
        uint8_t rotation = (uint8_t)((piece_rotation + 1u) % 4u);
        if (can_place(piece_x, piece_y, rotation)) {
            piece_rotation = rotation;
        }
    } else if (key == ' ') {
        while (can_place(piece_x, piece_y + 1, piece_rotation)) {
            ++piece_y;
        }
        lock_piece();
        last_fall_tick = pit_ticks_read();
    }
}

static uint8_t piece_color(uint8_t type)
{
    static const uint8_t colors[7] = {11u, 14u, 13u, 10u, 12u, 9u, 6u};
    return colors[type];
}

void tetris_draw(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    update_game();
    video_draw_text(x + 8, y + 3, "TETRIS", 2u);
    int32_t board_x = x + 8;
    int32_t board_y = y + 17;
    uint32_t board_width = TETRIS_COLUMNS * TETRIS_CELL_SIZE;
    uint32_t board_height = TETRIS_ROWS * TETRIS_CELL_SIZE;
    if (board_width + 84u > width || board_height + 22u > height) {
        return;
    }
    video_fill_rect(board_x - 2, board_y - 2, board_width + 4u,
                    board_height + 4u, 8u);
    video_fill_rect(board_x, board_y, board_width, board_height, 0u);
    for (uint32_t row = 0u; row < TETRIS_ROWS; ++row) {
        for (uint32_t column = 0u; column < TETRIS_COLUMNS; ++column) {
            uint8_t color = board[row][column] == 0u
                ? 0u : piece_color((uint8_t)(board[row][column] - 1u));
            if (color != 0u) {
                video_fill_rect(
                    board_x + (int32_t)column * TETRIS_CELL_SIZE + 1,
                    board_y + (int32_t)row * TETRIS_CELL_SIZE + 1,
                    TETRIS_CELL_SIZE - 1u, TETRIS_CELL_SIZE - 1u, color);
            }
        }
    }
    for (uint32_t local_y = 0u; local_y < 4u; ++local_y) {
        for (uint32_t local_x = 0u; local_x < 4u; ++local_x) {
            if (!piece_cell(piece_type, piece_rotation, local_x, local_y)) {
                continue;
            }
            int32_t cell_x = piece_x + (int32_t)local_x;
            int32_t cell_y = piece_y + (int32_t)local_y;
            if (cell_x >= 0 && cell_x < (int32_t)TETRIS_COLUMNS &&
                cell_y >= 0 && cell_y < (int32_t)TETRIS_ROWS) {
                video_fill_rect(
                    board_x + cell_x * TETRIS_CELL_SIZE + 1,
                    board_y + cell_y * TETRIS_CELL_SIZE + 1,
                    TETRIS_CELL_SIZE - 1u, TETRIS_CELL_SIZE - 1u,
                    piece_color(piece_type));
            }
        }
    }
    video_draw_text(board_x + (int32_t)board_width + 10, board_y + 5,
                    "A D MOVE", 1u);
    video_draw_text(board_x + (int32_t)board_width + 10, board_y + 17,
                    "W ROTATE", 1u);
    video_draw_text(board_x + (int32_t)board_width + 10, board_y + 29,
                    "S DOWN", 1u);
    video_draw_text(board_x + (int32_t)board_width + 10, board_y + 41,
                    "SPACE DROP", 1u);
    video_draw_text(board_x + (int32_t)board_width + 10, board_y + 57,
                    "LINES", 1u);
    char lines_text[11] = "0000000000";
    uint32_t value = lines_cleared;
    for (int32_t index = 9; index >= 0 && value != 0u; --index) {
        lines_text[index] = (char)('0' + value % 10u);
        value /= 10u;
    }
    video_draw_text(board_x + (int32_t)board_width + 10, board_y + 68,
                    lines_text, 1u);
    if (paused) {
        video_draw_text(board_x + 5, board_y + 43, "PAUSED", 14u);
    } else if (game_over) {
        video_draw_text(board_x + 2, board_y + 43, "GAME OVER", 12u);
    }
}
