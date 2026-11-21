#include <stdint.h>

#include "arena.h"
#include "arena_sprites.h"
#include "pit.h"
#include "video.h"

#define MAP_SIZE 10u
#define RAY_COUNT 128u
#define MAX_RAY_STEPS 192u
#define ENEMY_COUNT 6u
#define PICKUP_COUNT 4u
#define LEVEL_COUNT 8u
#define PLAYER_RADIUS 40
#define MAGAZINE_SIZE 12u
#define RESERVE_START 24u
#define RESERVE_MAX 60u

enum arena_state {
    ARENA_TITLE,
    ARENA_PLAYING,
    ARENA_WON,
    ARENA_LOST
};

enum enemy_type {
    ENEMY_GRUNT,
    ENEMY_RUNNER,
    ENEMY_BRUTE,
    ENEMY_BOSS
};

struct arena_enemy {
    uint8_t x;
    uint8_t y;
    uint8_t health;
    uint8_t alive;
};

struct arena_pickup {
    uint8_t x;
    uint8_t y;
    uint8_t kind; /* 1 = health, 2 = ammo */
    uint8_t active;
};

struct level_enemy {
    uint8_t x;
    uint8_t y;
    uint8_t type;
};

/* Per-type stats. height_eighths is sprite height relative to a wall. */
struct enemy_kind {
    uint8_t health;
    int8_t speed_offset;     /* added to the level's move interval (ticks) */
    uint8_t damage;
    uint16_t cooldown;       /* ticks between attacks */
    uint16_t score;
    uint8_t height_eighths;
    const char *art;         /* first character of frame 0 */
};

#define SPRITE_FRAME_SIZE (SPRITE_ROWS * (SPRITE_COLS + 1u))

/* grunt = raider, runner = skeleton, brute = armoured knight, boss = demon */
static const struct enemy_kind enemy_kinds[4] = {
    {3u,   0, 10u, 200u,  100u,  6u, &sprite_grunt[0][0][0]},
    {2u, -20,  8u, 140u,  150u,  6u, &sprite_runner[0][0][0]},
    {6u,  30, 20u, 260u,  250u,  8u, &sprite_brute[0][0][0]},
    {12u, 10, 25u, 180u, 1000u, 10u, &sprite_boss[0][0][0]},
};

static const char worlds[LEVEL_COUNT][MAP_SIZE][MAP_SIZE + 1u] = {
    {
        "##########",
        "#........#",
        "#..#.....#",
        "#........#",
        "#.....#..#",
        "#........#",
        "#..#.....#",
        "#........#",
        "#........#",
        "##########",
    },
    {
        "##########",
        "#........#",
        "#....#...#",
        "#........#",
        "#..#.....#",
        "#........#",
        "#.....#..#",
        "#........#",
        "#........#",
        "##########",
    },
    {
        "##########",
        "#........#",
        "#.##..##.#",
        "#........#",
        "#..#..#..#",
        "#........#",
        "#..#..#..#",
        "#........#",
        "#.##..##.#",
        "##########",
    },
    {
        "##########",
        "#........#",
        "#.#.##.#.#",
        "#........#",
        "#.#....#.#",
        "#.#....#.#",
        "#........#",
        "#.#.##.#.#",
        "#........#",
        "##########",
    },
    {
        "##########",
        "#........#",
        "#.######.#",
        "#........#",
        "#.#....#.#",
        "#.#....#.#",
        "#........#",
        "#.######.#",
        "#........#",
        "##########",
    },
    {
        "##########",
        "#........#",
        "#.####.#.#",
        "#......#.#",
        "#.####.#.#",
        "#........#",
        "#.#.####.#",
        "#.#......#",
        "#........#",
        "##########",
    },
    {
        "##########",
        "#...#....#",
        "#...#....#",
        "#........#",
        "###.##.###",
        "#........#",
        "#...##...#",
        "#...##...#",
        "#........#",
        "##########",
    },
    {
        "##########",
        "#........#",
        "#.#....#.#",
        "#........#",
        "#...##...#",
        "#...##...#",
        "#........#",
        "#.#....#.#",
        "#........#",
        "##########",
    },
};

static const uint8_t level_enemy_count[LEVEL_COUNT] = {2u, 2u, 4u, 4u, 5u, 5u, 6u, 6u};
static const uint8_t level_damage_bonus[LEVEL_COUNT] = {0u, 0u, 5u, 5u, 5u, 8u, 8u, 10u};
static const uint8_t level_enemy_speed[LEVEL_COUNT] = {60u, 60u, 45u, 45u, 42u, 40u, 38u, 36u};

static const struct level_enemy level_enemies[LEVEL_COUNT][ENEMY_COUNT] = {
    {{6u, 1u, 0u}, {4u, 7u, 0u}},
    {{7u, 3u, 0u}, {2u, 7u, 0u}},
    {{8u, 1u, 0u}, {8u, 8u, 1u}, {1u, 8u, 0u}, {5u, 5u, 0u}},
    {{8u, 1u, 1u}, {8u, 8u, 0u}, {1u, 8u, 0u}, {4u, 4u, 1u}},
    {{8u, 1u, 1u}, {8u, 8u, 0u}, {1u, 8u, 0u}, {5u, 4u, 2u}, {4u, 5u, 1u}},
    {{8u, 1u, 0u}, {7u, 7u, 2u}, {1u, 8u, 1u}, {8u, 8u, 1u}, {5u, 5u, 0u}},
    {{7u, 2u, 2u}, {2u, 7u, 1u}, {8u, 8u, 0u}, {6u, 5u, 0u}, {4u, 3u, 1u}, {8u, 6u, 0u}},
    {{6u, 6u, 3u}, {8u, 1u, 1u}, {1u, 8u, 1u}, {8u, 8u, 0u}, {3u, 3u, 0u}, {3u, 5u, 2u}},
};

static const struct arena_pickup level_pickups[LEVEL_COUNT][PICKUP_COUNT] = {
    {{8u, 8u, 1u, 1u}, {1u, 8u, 2u, 1u}, {8u, 4u, 2u, 1u}, {0u, 0u, 0u, 0u}},
    {{8u, 1u, 1u, 1u}, {1u, 8u, 2u, 1u}, {4u, 5u, 2u, 1u}, {0u, 0u, 0u, 0u}},
    {{4u, 3u, 1u, 1u}, {5u, 7u, 1u, 1u}, {5u, 3u, 2u, 1u}, {0u, 0u, 0u, 0u}},
    {{1u, 3u, 1u, 1u}, {8u, 6u, 1u, 1u}, {5u, 1u, 2u, 1u}, {5u, 8u, 2u, 1u}},
    {{1u, 6u, 1u, 1u}, {8u, 3u, 1u, 1u}, {8u, 8u, 2u, 1u}, {1u, 8u, 2u, 1u}},
    {{1u, 5u, 1u, 1u}, {8u, 5u, 1u, 1u}, {5u, 1u, 2u, 1u}, {8u, 8u, 2u, 1u}},
    {{1u, 8u, 1u, 1u}, {8u, 1u, 1u, 1u}, {5u, 3u, 2u, 1u}, {5u, 8u, 2u, 1u}},
    {{1u, 5u, 1u, 1u}, {8u, 5u, 1u, 1u}, {8u, 8u, 2u, 1u}, {1u, 8u, 2u, 1u}},
};

static const int8_t sine_quarter[17] = {
    0, 2, 3, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 15, 16, 16, 16,
};

static enum arena_state state;
static struct arena_enemy enemies[ENEMY_COUNT];
static uint32_t enemy_hurt_until[ENEMY_COUNT];
static uint8_t current_level;
static int32_t player_x;
static int32_t player_y;
static uint8_t player_direction;
static uint8_t player_health;
static uint8_t player_ammo;
static uint8_t player_reserve;
static uint8_t enemies_killed;
static uint8_t active_enemy_count;
static struct arena_pickup pickups[PICKUP_COUNT];
static uint32_t score;
static uint32_t level_start_score;
static int32_t mouse_turn_remainder;
static uint8_t enemy_type[ENEMY_COUNT];
static uint32_t enemy_next_move[ENEMY_COUNT];
static uint32_t enemy_next_attack[ENEMY_COUNT];
static uint32_t muzzle_flash_until;
static uint32_t hit_marker_until;
static uint32_t damage_flash_until;
static uint8_t wall_depth[RAY_COUNT];
static char last_action[12];

static int32_t sine_value(uint8_t offset)
{
    uint32_t index = offset >> 2;
    uint32_t fraction = offset & 3u;
    if (index >= 16u) {
        return sine_quarter[16];
    }
    return ((4u - fraction) * (uint32_t)sine_quarter[index] +
            fraction * (uint32_t)sine_quarter[index + 1u]) / 4u;
}

static int32_t direction_x(uint8_t angle)
{
    uint8_t quadrant = angle >> 6;
    uint8_t offset = angle & 63u;
    int32_t sine = sine_value(offset);
    int32_t cosine = sine_value((uint8_t)(64u - offset));
    if (quadrant == 0u) {
        return cosine;
    }
    if (quadrant == 1u) {
        return -sine;
    }
    if (quadrant == 2u) {
        return -cosine;
    }
    return sine;
}

static int32_t direction_y(uint8_t angle)
{
    uint8_t quadrant = angle >> 6;
    uint8_t offset = angle & 63u;
    int32_t sine = sine_value(offset);
    int32_t cosine = sine_value((uint8_t)(64u - offset));
    if (quadrant == 0u) {
        return sine;
    }
    if (quadrant == 1u) {
        return cosine;
    }
    if (quadrant == 2u) {
        return -sine;
    }
    return -cosine;
}

static uint32_t divide_u32(uint32_t numerator, uint32_t denominator)
{
    if (denominator == 0u) {
        return 0u;
    }
    uint32_t quotient = 0u;
    uint32_t remainder = 0u;
    for (int32_t bit = 31; bit >= 0; --bit) {
        remainder = (remainder << 1) | ((numerator >> bit) & 1u);
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= 1u << bit;
        }
    }
    return quotient;
}

static int is_wall(int32_t x, int32_t y)
{
    if (x < 0 || y < 0) {
        return 1;
    }
    uint32_t cell_x = (uint32_t)x >> 8;
    uint32_t cell_y = (uint32_t)y >> 8;
    return cell_x >= MAP_SIZE || cell_y >= MAP_SIZE ||
           worlds[current_level - 1u][cell_y][cell_x] == '#';
}

static int32_t divide_s32(int32_t numerator, int32_t denominator)
{
    uint32_t n = numerator < 0 ? (uint32_t)(-numerator) : (uint32_t)numerator;
    uint32_t d = denominator < 0 ? (uint32_t)(-denominator)
                                 : (uint32_t)denominator;
    uint32_t q = divide_u32(n, d);
    return ((numerator < 0) != (denominator < 0)) ? -(int32_t)q : (int32_t)q;
}

static void set_action(const char *text)
{
    uint32_t index = 0u;
    while (text[index] != '\0' && index + 1u < sizeof(last_action)) {
        last_action[index] = text[index];
        ++index;
    }
    last_action[index] = '\0';
}

static int enemy_at(int32_t x, int32_t y)
{
    if (x < 0 || y < 0) {
        return -1;
    }
    uint32_t cell_x = (uint32_t)x >> 8;
    uint32_t cell_y = (uint32_t)y >> 8;
    for (uint32_t index = 0u; index < ENEMY_COUNT; ++index) {
        if (enemies[index].alive && enemies[index].x == cell_x &&
            enemies[index].y == cell_y) {
            return (int)index;
        }
    }
    return -1;
}

/* True when the player's body (a small square) would overlap a wall. */
static int body_hits_wall(int32_t x, int32_t y)
{
    return is_wall(x - PLAYER_RADIUS, y - PLAYER_RADIUS) ||
           is_wall(x + PLAYER_RADIUS, y - PLAYER_RADIUS) ||
           is_wall(x - PLAYER_RADIUS, y + PLAYER_RADIUS) ||
           is_wall(x + PLAYER_RADIUS, y + PLAYER_RADIUS);
}

static uint32_t enemy_move_interval(uint32_t index)
{
    int32_t interval = (int32_t)level_enemy_speed[current_level - 1u] +
                       enemy_kinds[enemy_type[index]].speed_offset;
    return interval < 20 ? 20u : (uint32_t)interval;
}

static void reset_level(void)
{
    player_x = 384;
    player_y = 384;
    player_direction = 0u;
    player_health = 100u;
    player_ammo = MAGAZINE_SIZE;
    player_reserve = RESERVE_START;
    enemies_killed = 0u;
    mouse_turn_remainder = 0;
    score = level_start_score;
    uint32_t ticks = pit_ticks_read();
    active_enemy_count = level_enemy_count[current_level - 1u];
    for (uint32_t index = 0u; index < ENEMY_COUNT; ++index) {
        enemies[index] = (struct arena_enemy){0u, 0u, 0u, 0u};
        enemy_type[index] = ENEMY_GRUNT;
        enemy_hurt_until[index] = 0u;
        if (index < active_enemy_count) {
            const struct level_enemy *spawn =
                &level_enemies[current_level - 1u][index];
            enemy_type[index] = spawn->type;
            enemies[index] = (struct arena_enemy){
                spawn->x, spawn->y, enemy_kinds[spawn->type].health, 1u};
        }
        enemy_next_move[index] = ticks + enemy_move_interval(index);
        enemy_next_attack[index] = ticks + 200u;
    }
    for (uint32_t index = 0u; index < PICKUP_COUNT; ++index) {
        pickups[index] = level_pickups[current_level - 1u][index];
    }
    muzzle_flash_until = 0u;
    hit_marker_until = 0u;
    damage_flash_until = 0u;
    last_action[0] = '\0';
}

void arena_initialize(void)
{
    state = ARENA_TITLE;
    current_level = 1u;
    level_start_score = 0u;
    reset_level();
}

static void collect_pickups(void)
{
    uint32_t cell_x = (uint32_t)player_x >> 8;
    uint32_t cell_y = (uint32_t)player_y >> 8;
    for (uint32_t index = 0u; index < PICKUP_COUNT; ++index) {
        struct arena_pickup *pickup = &pickups[index];
        if (!pickup->active || pickup->x != cell_x || pickup->y != cell_y) {
            continue;
        }
        if (pickup->kind == 1u) {
            if (player_health >= 100u) {
                continue; /* keep it for later */
            }
            player_health = player_health > 75u
                                ? 100u
                                : (uint8_t)(player_health + 25u);
            set_action("HEALTH");
            pit_beep(990u, 3u);
        } else {
            if (player_reserve >= RESERVE_MAX) {
                continue;
            }
            player_reserve = player_reserve + 12u > RESERVE_MAX
                                 ? RESERVE_MAX
                                 : (uint8_t)(player_reserve + 12u);
            set_action("AMMO");
            pit_beep(740u, 3u);
        }
        pickup->active = 0u;
    }
}

static void move_player(int32_t dx, int32_t dy)
{
    int32_t next_x = player_x + dx;
    int32_t next_y = player_y + dy;
    if (!body_hits_wall(next_x, player_y) &&
        enemy_at(next_x, player_y) < 0) {
        player_x = next_x;
    }
    if (!body_hits_wall(player_x, next_y) &&
        enemy_at(player_x, next_y) < 0) {
        player_y = next_y;
    }
    collect_pickups();
}

static int enemy_can_enter(uint32_t index, int32_t x, int32_t y)
{
    if (x < 0 || y < 0 || x >= (int32_t)MAP_SIZE ||
        y >= (int32_t)MAP_SIZE ||
        worlds[current_level - 1u][y][x] == '#' ||
        ((uint32_t)x == ((uint32_t)player_x >> 8) &&
         (uint32_t)y == ((uint32_t)player_y >> 8))) {
        return 0;
    }
    for (uint32_t other = 0u; other < ENEMY_COUNT; ++other) {
        if (other != index && enemies[other].alive &&
            enemies[other].x == (uint32_t)x &&
            enemies[other].y == (uint32_t)y) {
            return 0;
        }
    }
    return 1;
}

static int try_move_enemy(uint32_t index, int32_t step_x, int32_t step_y)
{
    int32_t next_x = (int32_t)enemies[index].x + step_x;
    int32_t next_y = (int32_t)enemies[index].y + step_y;
    if (!enemy_can_enter(index, next_x, next_y)) {
        return 0;
    }
    enemies[index].x = (uint8_t)next_x;
    enemies[index].y = (uint8_t)next_y;
    return 1;
}

static void move_enemy_toward_player(uint32_t index)
{
    static const int8_t step_x[4] = {1, -1, 0, 0};
    static const int8_t step_y[4] = {0, 0, 1, -1};
    uint8_t queue[MAP_SIZE * MAP_SIZE];
    int8_t first_step[MAP_SIZE * MAP_SIZE];
    uint32_t start = (uint32_t)enemies[index].y * MAP_SIZE +
                     enemies[index].x;
    uint32_t player_cell_x = (uint32_t)player_x >> 8;
    uint32_t player_cell_y = (uint32_t)player_y >> 8;
    uint32_t head = 0u;
    uint32_t tail = 0u;

    for (uint32_t cell = 0u; cell < MAP_SIZE * MAP_SIZE; ++cell) {
        first_step[cell] = -1;
    }
    first_step[start] = 0;
    queue[tail++] = (uint8_t)start;

    while (head < tail) {
        uint32_t cell = queue[head++];
        int32_t x = (int32_t)(cell % MAP_SIZE);
        int32_t y = (int32_t)(cell / MAP_SIZE);
        uint32_t distance_x = x < (int32_t)player_cell_x
                                 ? player_cell_x - (uint32_t)x
                                 : (uint32_t)x - player_cell_x;
        uint32_t distance_y = y < (int32_t)player_cell_y
                                 ? player_cell_y - (uint32_t)y
                                 : (uint32_t)y - player_cell_y;
        if (distance_x + distance_y == 1u) {
            if (cell != start) {
                uint32_t direction = (uint32_t)first_step[cell];
                (void)try_move_enemy(index, step_x[direction],
                                     step_y[direction]);
            }
            return;
        }

        for (uint32_t direction = 0u; direction < 4u; ++direction) {
            int32_t next_x = x + step_x[direction];
            int32_t next_y = y + step_y[direction];
            if (!enemy_can_enter(index, next_x, next_y)) {
                continue;
            }
            uint32_t next = (uint32_t)next_y * MAP_SIZE + (uint32_t)next_x;
            if (first_step[next] >= 0) {
                continue;
            }
            first_step[next] = cell == start
                                   ? (int8_t)direction
                                   : first_step[cell];
            queue[tail++] = (uint8_t)next;
        }
    }
}

static void update_enemies(void)
{
    uint32_t ticks = pit_ticks_read();
    for (uint32_t index = 0u; index < ENEMY_COUNT; ++index) {
        if (!enemies[index].alive) {
            continue;
        }
        const struct enemy_kind *kind = &enemy_kinds[enemy_type[index]];
        if ((int32_t)(ticks - enemy_next_move[index]) >= 0) {
            enemy_next_move[index] = ticks + enemy_move_interval(index);
            move_enemy_toward_player(index);
        }

        int32_t delta_x = (int32_t)enemies[index].x * 256 + 128 - player_x;
        int32_t delta_y = (int32_t)enemies[index].y * 256 + 128 - player_y;
        if (delta_x < 0) {
            delta_x = -delta_x;
        }
        if (delta_y < 0) {
            delta_y = -delta_y;
        }
        if (delta_x >= 320 || delta_y >= 320) {
            /* wind-up: a fresh arrival cannot strike immediately */
            if ((int32_t)(enemy_next_attack[index] - (ticks + 40u)) < 0) {
                enemy_next_attack[index] = ticks + 40u;
            }
            continue;
        }
        if ((int32_t)(ticks - enemy_next_attack[index]) < 0) {
            continue;
        }
        enemy_next_attack[index] = ticks + kind->cooldown;
        uint32_t damage = (uint32_t)kind->damage +
                          level_damage_bonus[current_level - 1u];
        player_health = player_health > damage
                            ? (uint8_t)(player_health - damage)
                            : 0u;
        damage_flash_until = ticks + 12u;
        pit_beep(196u, 5u);
        if (player_health == 0u) {
            state = ARENA_LOST;
            return;
        }
    }
}

void arena_fire(void)
{
    if (state != ARENA_PLAYING) {
        return;
    }
    if (player_ammo == 0u) {
        set_action(player_reserve != 0u ? "RELOAD" : "EMPTY");
        pit_beep(220u, 3u);
        return;
    }
    --player_ammo;
    muzzle_flash_until = pit_ticks_read() + 4u;
    pit_beep(880u, 2u);
    for (uint32_t step = 1u; step <= MAX_RAY_STEPS; ++step) {
        int32_t hit_x = player_x +
                        direction_x(player_direction) * (int32_t)step;
        int32_t hit_y = player_y +
                        direction_y(player_direction) * (int32_t)step;
        if (is_wall(hit_x, hit_y)) {
            break;
        }
        int enemy = enemy_at(hit_x, hit_y);
        if (enemy >= 0) {
            struct arena_enemy *target = &enemies[(uint32_t)enemy];
            if (target->health > 0u) {
                --target->health;
            }
            enemy_hurt_until[enemy] = pit_ticks_read() + 10u;
            hit_marker_until = pit_ticks_read() + 10u;
            if (target->health == 0u) {
                target->alive = 0u;
                ++enemies_killed;
                score += enemy_kinds[enemy_type[enemy]].score;
                if (enemies_killed >= active_enemy_count) {
                    score += (uint32_t)player_health * 2u;
                    state = ARENA_WON;
                }
            }
            pit_beep(1320u, 3u);
            set_action(target->alive ? "HIT" : "KILL");
            return;
        }
    }
    set_action("MISS");
}

static void reload_weapon(void)
{
    if (player_ammo >= MAGAZINE_SIZE) {
        return;
    }
    if (player_reserve == 0u) {
        set_action("NO RESERVE");
        pit_beep(220u, 3u);
        return;
    }
    uint8_t needed = (uint8_t)(MAGAZINE_SIZE - player_ammo);
    uint8_t taken = needed < player_reserve ? needed : player_reserve;
    player_ammo = (uint8_t)(player_ammo + taken);
    player_reserve = (uint8_t)(player_reserve - taken);
    set_action("RELOAD");
    pit_beep(660u, 3u);
}

void arena_handle_key(char key)
{
    if (key == '\n' && state != ARENA_PLAYING) {
        if (state == ARENA_WON) {
            if (current_level < LEVEL_COUNT) {
                current_level = (uint8_t)(current_level + 1u);
                level_start_score = score;
            } else {
                current_level = 1u;
                level_start_score = 0u;
            }
        } else if (state == ARENA_TITLE) {
            current_level = 1u;
            level_start_score = 0u;
        }
        /* ARENA_LOST retries the same level with the score it began with. */
        reset_level();
        state = ARENA_PLAYING;
        return;
    }
    if (state != ARENA_PLAYING) {
        return;
    }

    int32_t forward_x = direction_x(player_direction) * 3;
    int32_t forward_y = direction_y(player_direction) * 3;
    int32_t side_x = -direction_y(player_direction) * 3;
    int32_t side_y = direction_x(player_direction) * 3;
    if (key == 'w' || key == 'W') {
        move_player(forward_x, forward_y);
    } else if (key == 's' || key == 'S') {
        move_player(-forward_x, -forward_y);
    } else if (key == 'a' || key == 'A') {
        move_player(-side_x, -side_y);
    } else if (key == 'd' || key == 'D') {
        move_player(side_x, side_y);
    } else if (key == 'q' || key == 'Q') {
        player_direction = (uint8_t)(player_direction - 4u);
    } else if (key == 'e' || key == 'E') {
        player_direction = (uint8_t)(player_direction + 4u);
    } else if (key == 'f' || key == 'F' || key == ' ') {
        arena_fire();
    } else if (key == 'r' || key == 'R') {
        reload_weapon();
    }
}

void arena_handle_mouse(int32_t delta_x)
{
    if (state != ARENA_PLAYING) {
        return;
    }
    mouse_turn_remainder += delta_x;
    while (mouse_turn_remainder >= 4) {
        player_direction = (uint8_t)(player_direction + 4u);
        mouse_turn_remainder -= 4;
    }
    while (mouse_turn_remainder <= -4) {
        player_direction = (uint8_t)(player_direction - 4u);
        mouse_turn_remainder += 4;
    }
}

static void draw_number(int32_t x, int32_t y, uint32_t value, uint8_t color)
{
    char digits[6];
    uint32_t count = 0u;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));
    while (count != 0u) {
        char character[2] = {digits[--count], '\0'};
        video_draw_text(x, y, character, color);
        x += 6;
    }
}

static void draw_title(int32_t x, int32_t y, uint32_t width,
                       uint32_t height)
{
    video_fill_rect(x, y, width, height, 1u);
    video_fill_rect(x + 12, y + 6, width > 24u ? width - 24u : width, 16u, 5u);
    video_draw_text(x + 24, y + 10, "TEXUGOS ARENA", 15u);
    video_draw_text(x + 22, y + 34, "ENTER TO START", 11u);
    video_draw_text(x + 16, y + 54, "WASD MOVE Q E TURN", 15u);
    video_draw_text(x + 16, y + 69, "F FIRE R RELOAD", 15u);
    video_draw_text(x + 16, y + 84, "MOUSE AIM CLICK FIRE", 15u);
    if (height > 110u) {
        video_draw_text(x + 16, y + 99, "8 LEVELS  FIND PACKS", 14u);
    }
}

static void draw_end_screen(int32_t x, int32_t y, uint32_t width,
                            uint32_t height)
{
    int won = state == ARENA_WON;
    video_fill_rect(x, y, width, height, 1u);
    video_draw_text(x + 28, y + 24,
                    won ? (current_level == LEVEL_COUNT ? "GAME COMPLETE"
                                                        : "LEVEL CLEAR")
                        : "GAME OVER",
                    won ? 10u : 4u);
    video_draw_text(x + 18, y + 48,
                    won && current_level < LEVEL_COUNT ? "ENTER NEXT LEVEL"
                                                       : "ENTER RESTART",
                    15u);
    video_draw_text(x + 18, y + 68, "ESC DESKTOP", 15u);
    if (height > 100u) {
        video_draw_text(x + 18, y + 86, "SCORE", 14u);
        draw_number(x + 54, y + 86, score, 14u);
    }
}

static void draw_floor_and_ceiling(int32_t x, int32_t y, uint32_t width,
                                   uint32_t view_height, uint32_t horizon)
{
    uint32_t band = horizon / 3u;
    video_fill_rect(x, y, width, horizon, 1u);
    video_fill_rect(x, y, width, band, 0u);
    video_fill_rect(x, y + (int32_t)horizon, width,
                    view_height - horizon, 8u);
    video_fill_rect(x, y + (int32_t)horizon, width, band, 2u);
    video_fill_rect(x, y + (int32_t)(horizon + band), width, band, 3u);
}

static void draw_walls(int32_t x, int32_t y, uint32_t width,
                       uint32_t view_height, uint32_t horizon)
{
    /* bright palette for N/S faces, dimmer one for E/W faces */
    static const uint8_t lit[3] = {15u, 11u, 9u};
    static const uint8_t dim[3] = {11u, 9u, 5u};

    for (uint32_t column = 0u; column < RAY_COUNT; ++column) {
        int32_t angle_offset =
            (int32_t)(column * 64u / RAY_COUNT) - 32;
        uint8_t angle = (uint8_t)((int32_t)player_direction + angle_offset);
        int32_t ray_x = player_x;
        int32_t ray_y = player_y;
        int32_t step_x = direction_x(angle);
        int32_t step_y = direction_y(angle);
        uint32_t distance = MAX_RAY_STEPS;
        uint8_t wall_color = 5u;
        for (uint32_t step = 1u; step <= MAX_RAY_STEPS; ++step) {
            int32_t previous_x = ray_x;
            ray_x += step_x;
            ray_y += step_y;
            if (is_wall(ray_x, ray_y)) {
                uint32_t shade = step < 32u ? 0u : (step < 72u ? 1u : 2u);
                int east_west = ray_x >= 0 && previous_x >= 0 &&
                                ((uint32_t)ray_x >> 8) !=
                                    ((uint32_t)previous_x >> 8);
                distance = step;
                wall_color = east_west ? dim[shade] : lit[shade];
                break;
            }
        }
        wall_depth[column] = distance > 255u ? 255u : (uint8_t)distance;

        uint32_t wall_height = divide_u32(view_height * 32u, distance);
        if (wall_height > view_height) {
            wall_height = view_height;
        }
        uint32_t left = column * width / RAY_COUNT;
        uint32_t right = (column + 1u) * width / RAY_COUNT;
        uint32_t top = horizon > wall_height / 2u
                           ? horizon - wall_height / 2u
                           : 0u;
        video_fill_rect(x + (int32_t)left, y + (int32_t)top,
                        right - left, wall_height, wall_color);
    }
}

/*
 * Billboard sprite renderer. Coordinates are relative to the 3D view.
 * Each sprite column is clipped against the view and hidden behind walls
 * closer than `distance`; vertical runs of one colour become single rects.
 */
static void draw_sprite(int32_t view_x, int32_t view_y, uint32_t view_w,
                        uint32_t view_h, const char *art, uint32_t cols,
                        uint32_t rows, int32_t center_x, int32_t bottom,
                        uint32_t height, uint32_t distance, int hurt)
{
    int32_t x_edge[SPRITE_COLS + 1u];
    int32_t y_edge[SPRITE_ROWS + 1u];
    uint32_t width = divide_u32(height * cols, rows);
    int32_t left = center_x - (int32_t)width / 2;
    int32_t top = bottom - (int32_t)height;
    uint32_t stride = cols + 1u;

    if (height < 2u || width < 2u) {
        return;
    }
    for (uint32_t c = 0u; c <= cols; ++c) {
        x_edge[c] = left + (int32_t)divide_u32(width * c, cols);
    }
    for (uint32_t r = 0u; r <= rows; ++r) {
        y_edge[r] = top + (int32_t)divide_u32(height * r, rows);
    }

    for (uint32_t c = 0u; c < cols; ++c) {
        int32_t x0 = x_edge[c];
        int32_t x1 = x_edge[c + 1u];
        if (x1 <= x0 || x1 <= 0 || x0 >= (int32_t)view_w) {
            continue;
        }
        int32_t mid = (x0 + x1) / 2;
        if (mid < 0) {
            mid = 0;
        }
        if (mid >= (int32_t)view_w) {
            mid = (int32_t)view_w - 1;
        }
        uint32_t ray = divide_u32((uint32_t)mid * RAY_COUNT, view_w);
        if (ray < RAY_COUNT && (uint32_t)wall_depth[ray] < distance) {
            continue;
        }
        if (x0 < 0) {
            x0 = 0;
        }
        if (x1 > (int32_t)view_w) {
            x1 = (int32_t)view_w;
        }

        uint32_t r = 0u;
        while (r < rows) {
            char pixel = art[r * stride + c];
            if (pixel == '.') {
                ++r;
                continue;
            }
            uint32_t run_end = r + 1u;
            while (run_end < rows && art[run_end * stride + c] == pixel) {
                ++run_end;
            }
            int32_t y0 = y_edge[r];
            int32_t y1 = y_edge[run_end];
            if (y0 < 0) {
                y0 = 0;
            }
            if (y1 > (int32_t)view_h) {
                y1 = (int32_t)view_h;
            }
            if (y1 > y0) {
                uint8_t color = pixel <= '9' ? (uint8_t)(pixel - '0')
                                             : (uint8_t)(pixel - 'a' + 10);
                if (hurt && color != 0u) {
                    color = 15u;
                }
                video_fill_rect(view_x + x0, view_y + y0, (uint32_t)(x1 - x0),
                                (uint32_t)(y1 - y0), color);
            }
            r = run_end;
        }
    }
}

struct sprite_entry {
    uint32_t distance;
    int32_t center;
    uint8_t is_enemy;
    uint8_t index;
};

static void draw_sprites(int32_t x, int32_t y, uint32_t width,
                         uint32_t view_height, uint32_t horizon)
{
    struct sprite_entry list[ENEMY_COUNT + PICKUP_COUNT];
    uint32_t count = 0u;
    int32_t dir_x = direction_x(player_direction);
    int32_t dir_y = direction_y(player_direction);
    uint32_t ticks = pit_ticks_read();

    for (uint32_t entry = 0u; entry < ENEMY_COUNT + PICKUP_COUNT; ++entry) {
        int is_enemy = entry < ENEMY_COUNT;
        uint32_t index = is_enemy ? entry : entry - ENEMY_COUNT;
        int32_t cell_x;
        int32_t cell_y;
        if (is_enemy) {
            if (!enemies[index].alive) {
                continue;
            }
            cell_x = enemies[index].x;
            cell_y = enemies[index].y;
        } else {
            if (!pickups[index].active) {
                continue;
            }
            cell_x = pickups[index].x;
            cell_y = pickups[index].y;
        }
        int32_t delta_x = cell_x * 256 + 128 - player_x;
        int32_t delta_y = cell_y * 256 + 128 - player_y;
        int32_t forward = delta_x * dir_x + delta_y * dir_y;
        int32_t side = -delta_x * dir_y + delta_y * dir_x;
        if (forward < 512) {
            continue;
        }
        int32_t abs_x = delta_x < 0 ? -delta_x : delta_x;
        int32_t abs_y = delta_y < 0 ? -delta_y : delta_y;
        int32_t far = abs_x > abs_y ? abs_x : abs_y;
        int32_t near = abs_x > abs_y ? abs_y : abs_x;
        uint32_t distance = (uint32_t)(far + near / 2) >> 4;
        if (distance < 4u) {
            distance = 4u;
        }
        int32_t column = (int32_t)(RAY_COUNT / 2u) +
                         divide_s32(side * (int32_t)(RAY_COUNT / 2u),
                                    forward);
        if (column < -48 || column > (int32_t)RAY_COUNT + 48) {
            continue;
        }
        list[count].distance = distance;
        list[count].center = column * (int32_t)width / (int32_t)RAY_COUNT;
        list[count].is_enemy = (uint8_t)is_enemy;
        list[count].index = (uint8_t)index;
        /* insertion sort, farthest first */
        uint32_t slot = count++;
        while (slot > 0u && list[slot - 1u].distance < list[slot].distance) {
            struct sprite_entry swap = list[slot - 1u];
            list[slot - 1u] = list[slot];
            list[slot] = swap;
            --slot;
        }
    }

    for (uint32_t item = 0u; item < count; ++item) {
        uint32_t wall_height = divide_u32(view_height * 32u,
                                          list[item].distance);
        if (wall_height > view_height) {
            wall_height = view_height;
        }
        int32_t bottom = (int32_t)horizon + (int32_t)wall_height / 2;
        uint32_t index = list[item].index;
        if (list[item].is_enemy) {
            const struct enemy_kind *kind = &enemy_kinds[enemy_type[index]];
            uint32_t frame = ((ticks >> 4) + index) & 1u;
            int hurt = (int32_t)(ticks - enemy_hurt_until[index]) < 0;
            draw_sprite(x, y, width, view_height,
                        kind->art + frame * SPRITE_FRAME_SIZE, SPRITE_COLS,
                        SPRITE_ROWS, list[item].center, bottom,
                        wall_height * kind->height_eighths / 8u,
                        list[item].distance, hurt);
        } else {
            uint32_t size = wall_height / 4u;
            if (size < 6u) {
                size = 6u;
            }
            draw_sprite(x, y, width, view_height,
                        pickups[index].kind == 1u
                            ? &pickup_health[0][0]
                            : &pickup_ammo[0][0],
                        PICKUP_COLS, PICKUP_ROWS, list[item].center, bottom,
                        size, list[item].distance, 0);
        }
    }
}

static void draw_minimap(int32_t x, int32_t y, uint32_t width)
{
    const int32_t scale = 3;
    int32_t map_x = x + (int32_t)width - (int32_t)MAP_SIZE * scale - 3;
    int32_t map_y = y + 3;
    video_fill_rect(map_x - 1, map_y - 1, MAP_SIZE * (uint32_t)scale + 2u,
                    MAP_SIZE * (uint32_t)scale + 2u, 0u);
    for (uint32_t row = 0u; row < MAP_SIZE; ++row) {
        for (uint32_t col = 0u; col < MAP_SIZE; ++col) {
            if (worlds[current_level - 1u][row][col] == '#') {
                video_fill_rect(map_x + (int32_t)col * scale,
                                map_y + (int32_t)row * scale,
                                (uint32_t)scale, (uint32_t)scale, 7u);
            }
        }
    }
    for (uint32_t index = 0u; index < PICKUP_COUNT; ++index) {
        if (pickups[index].active) {
            video_fill_rect(map_x + (int32_t)pickups[index].x * scale + 1,
                            map_y + (int32_t)pickups[index].y * scale + 1,
                            1u, 1u, 14u);
        }
    }
    for (uint32_t index = 0u; index < ENEMY_COUNT; ++index) {
        if (enemies[index].alive) {
            video_fill_rect(map_x + (int32_t)enemies[index].x * scale,
                            map_y + (int32_t)enemies[index].y * scale,
                            (uint32_t)scale, (uint32_t)scale, 13u);
        }
    }
    int32_t px = map_x + (player_x * scale) / 256;
    int32_t py = map_y + (player_y * scale) / 256;
    video_fill_rect(px, py, 2u, 2u, 15u);
    video_fill_rect(px + (direction_x(player_direction) * 3) / 16,
                    py + (direction_y(player_direction) * 3) / 16,
                    1u, 1u, 11u);
}

static void draw_weapon(int32_t x, int32_t y, uint32_t width,
                        uint32_t view_height, int flash)
{
    int32_t center = x + (int32_t)width / 2;
    int32_t base = y + (int32_t)view_height;
    video_fill_rect(center - 9, base - 8, 18u, 8u, 6u);
    video_fill_rect(center - 6, base - 12, 12u, 5u, 7u);
    video_fill_rect(center - 2, base - 17, 4u, 6u, 8u);
    if (flash) {
        video_fill_rect(center - 5, base - 25, 10u, 8u, 14u);
        video_fill_rect(center - 2, base - 28, 4u, 4u, 15u);
    }
}

static void draw_hud(int32_t x, int32_t y, uint32_t width,
                     uint32_t view_height)
{
    int32_t row = y + (int32_t)view_height;
    video_draw_text(x + 2, row + 2, "HP", 1u);
    draw_number(x + 14, row + 2, player_health, 1u);
    video_draw_text(x + 42, row + 2, "AMMO", 1u);
    draw_number(x + 72, row + 2, player_ammo, 1u);
    video_draw_text(x + 88, row + 2, "/", 1u);
    draw_number(x + 94, row + 2, player_reserve, 1u);
    video_draw_text(x + 118, row + 2, "K", 1u);
    draw_number(x + 126, row + 2, enemies_killed, 1u);
    video_draw_text(x + 140, row + 2, "L", 1u);
    draw_number(x + 148, row + 2, current_level, 1u);
    if (last_action[0] != '\0') {
        video_draw_text(x + (int32_t)width - 62, row + 2, last_action, 4u);
    }

    /* health bar (100 px) and magazine pips */
    video_fill_rect(x + 2, row + 14, 102u, 7u, 7u);
    video_fill_rect(x + 3, row + 15, 100u, 5u, 0u);
    if (player_health != 0u) {
        video_fill_rect(x + 3, row + 15, player_health, 5u,
                        player_health > 40u ? 11u : 13u);
    }
    for (uint32_t pip = 0u; pip < MAGAZINE_SIZE; ++pip) {
        video_fill_rect(x + 112 + (int32_t)pip * 6, row + 14, 4u, 7u,
                        pip < player_ammo ? 14u : 3u);
    }
}

static void draw_boss_bar(int32_t x, int32_t y)
{
    for (uint32_t index = 0u; index < ENEMY_COUNT; ++index) {
        if (enemies[index].alive && enemy_type[index] == ENEMY_BOSS) {
            video_fill_rect(x + 2, y + 2, 98u, 6u, 0u);
            video_fill_rect(x + 3, y + 3, (uint32_t)enemies[index].health * 8u,
                            4u, 13u);
            video_draw_text(x + 2, y + 10, "BOSS", 13u);
            return;
        }
    }
}

void arena_draw(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    if (state == ARENA_TITLE) {
        draw_title(x, y, width, height);
        return;
    }
    if (state == ARENA_WON || state == ARENA_LOST) {
        draw_end_screen(x, y, width, height);
        return;
    }

    update_enemies();
    if (state != ARENA_PLAYING) {
        arena_draw(x, y, width, height);
        return;
    }

    uint32_t ticks = pit_ticks_read();
    uint32_t view_height = height > 36u ? height - 34u : height;
    uint32_t horizon = view_height / 2u;

    draw_floor_and_ceiling(x, y, width, view_height, horizon);
    draw_walls(x, y, width, view_height, horizon);
    draw_sprites(x, y, width, view_height, horizon);
    draw_minimap(x, y, width);
    draw_boss_bar(x, y);

    int hit_marker = (int32_t)(ticks - hit_marker_until) < 0;
    int flash = (int32_t)(ticks - muzzle_flash_until) < 0;
    uint8_t cross = hit_marker ? 13u : 15u;
    int32_t center_x = x + (int32_t)width / 2;
    video_fill_rect(center_x - 3, y + (int32_t)horizon - 1, 7u, 1u, cross);
    video_fill_rect(center_x, y + (int32_t)horizon - 3, 1u, 5u, cross);
    if (hit_marker) {
        video_fill_rect(center_x - 5, y + (int32_t)horizon - 5, 2u, 2u, cross);
        video_fill_rect(center_x + 4, y + (int32_t)horizon - 5, 2u, 2u, cross);
        video_fill_rect(center_x - 5, y + (int32_t)horizon + 3, 2u, 2u, cross);
        video_fill_rect(center_x + 4, y + (int32_t)horizon + 3, 2u, 2u, cross);
    }
    draw_weapon(x, y, width, view_height, flash);

    if ((int32_t)(ticks - damage_flash_until) < 0) {
        video_fill_rect(x, y, width, 2u, 13u);
        video_fill_rect(x, y + (int32_t)view_height - 2, width, 2u, 13u);
        video_fill_rect(x, y, 2u, view_height, 13u);
        video_fill_rect(x + (int32_t)width - 2, y, 2u, view_height, 13u);
    }
    draw_hud(x, y, width, view_height);
}