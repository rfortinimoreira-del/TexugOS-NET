#include <stdint.h>

#include "console.h"
#include "desktop.h"
#include "shell.h"
#include "terminal.h"
#include "video.h"

#define TERMINAL_SCROLLBACK 16384u
#define TERMINAL_INPUT_SIZE 128u
#define TERMINAL_CELL_WIDTH 6
#define TERMINAL_ROW_HEIGHT 9
#define TERMINAL_PADDING_X 5
#define TERMINAL_PADDING_Y 3
#define SCROLLBAR_WIDTH 9
#define SCROLLBAR_BUTTON 10
#define SCROLL_STEP 3u

#define COLOR_BACKGROUND 0u
#define COLOR_OUTPUT 7u
#define COLOR_INPUT 15u
#define COLOR_PROMPT 10u
#define COLOR_ART 14u

/* Historico: texto corrido (as quebras de linha da janela sao calculadas
 * ao desenhar, entao redimensionar a janela reorganiza o texto). */
static char history_text[TERMINAL_SCROLLBACK];
static uint8_t history_color[TERMINAL_SCROLLBACK];
static uint32_t history_length;
static uint8_t current_color;

static char input_text[TERMINAL_INPUT_SIZE];
static uint32_t input_length;
static uint8_t close_requested;

/* Rolagem: linhas puxadas para tras a partir do fim do texto. */
static uint32_t scroll_offset;
static uint32_t scroll_max;
static uint32_t scroll_rows;
static int32_t scroll_x;
static int32_t scroll_area_height;
static int32_t scroll_track_y;
static int32_t scroll_track_height;
static int32_t scroll_thumb_y;
static int32_t scroll_thumb_height;
static int32_t scroll_grab;
static uint8_t scroll_dragging;

static int text_equal(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

/* Descarta a linha mais antiga quando o historico enche. */
static void history_make_room(void)
{
    uint32_t drop = 0u;
    while (drop < history_length && history_text[drop] != '\n') {
        ++drop;
    }
    if (drop < history_length) {
        ++drop;
    }
    if (drop == 0u || drop > history_length) {
        drop = history_length / 2u + 1u;
    }
    for (uint32_t index = drop; index < history_length; ++index) {
        history_text[index - drop] = history_text[index];
        history_color[index - drop] = history_color[index];
    }
    history_length -= drop;
}

static void history_put(char character)
{
    if (character == '\b') {
        if (history_length != 0u &&
            history_text[history_length - 1u] != '\n') {
            --history_length;
        }
        return;
    }
    if (character != '\n' && (character < ' ' || character > '~')) {
        return;
    }
    if (history_length + 1u >= TERMINAL_SCROLLBACK) {
        history_make_room();
    }
    history_text[history_length] = character;
    history_color[history_length] = current_color;
    ++history_length;
}

static void history_clear(void)
{
    history_length = 0u;
}

static void history_write(const char *text, uint8_t color)
{
    uint8_t saved = current_color;
    current_color = color;
    while (*text != '\0') {
        history_put(*text++);
    }
    current_color = saved;
}

/* Imprime o prompt do shell (o mesmo da tela de boot) em verde. */
static void print_prompt(void)
{
    console_redirect(history_put, history_clear);
    current_color = COLOR_PROMPT;
    shell_print_prompt();
    current_color = COLOR_OUTPUT;
    console_redirect(0, 0);
}

void terminal_initialize(void)
{
    history_length = 0u;
    current_color = COLOR_OUTPUT;
    input_length = 0u;
    input_text[0] = '\0';
    close_requested = 0u;
    scroll_offset = 0u;
    scroll_dragging = 0u;
    history_write("TexugOS Terminal\n", COLOR_INPUT);
    history_write("Type 'help' for commands.\n", COLOR_OUTPUT);
    history_write("Try 'texugo'.\n", COLOR_OUTPUT);
    print_prompt();
}

static void run_command(void)
{
    char line[TERMINAL_INPUT_SIZE];
    for (uint32_t index = 0u; index <= input_length; ++index) {
        line[index] = input_text[index];
    }

    /* Eco do que foi digitado, como no shell de boot. */
    history_write(input_text, COLOR_INPUT);
    history_write("\n", COLOR_INPUT);
    input_length = 0u;
    input_text[0] = '\0';

    const char *name = line;
    while (*name == ' ') {
        ++name;
    }
    char word[16];
    uint32_t word_length = 0u;
    while (name[word_length] != '\0' && name[word_length] != ' ' &&
           word_length + 1u < sizeof(word)) {
        word[word_length] = name[word_length];
        ++word_length;
    }
    word[word_length] = '\0';
    int word_is_complete = name[word_length] == '\0' ||
                           name[word_length] == ' ';

    if (word_is_complete && text_equal(word, "exit")) {
        close_requested = 1u;
        return;
    }

    /* Mostra o comando na tela antes de comecar (alguns demoram). */
    desktop_refresh();

    console_redirect(history_put, history_clear);
    current_color = word_is_complete &&
                    (text_equal(word, "texugo") || text_equal(word, "badger"))
                        ? COLOR_ART : COLOR_OUTPUT;
    shell_execute_line(line);
    if (word_is_complete && text_equal(word, "help")) {
        history_write("  exit (close this window)\n", COLOR_OUTPUT);
    }
    current_color = COLOR_OUTPUT;
    console_redirect(0, 0);

    /* `clear` apaga tudo; garante que o prompt comece em linha nova. */
    if (history_length != 0u && history_text[history_length - 1u] != '\n') {
        history_write("\n", COLOR_OUTPUT);
    }
    print_prompt();
}

void terminal_handle_key(char key)
{
    scroll_offset = 0u;
    if (key == '\n') {
        run_command();
    } else if (key == '\b') {
        if (input_length != 0u) {
            input_text[--input_length] = '\0';
        }
    } else if (key >= ' ' && key <= '~' &&
               input_length + 1u < TERMINAL_INPUT_SIZE) {
        input_text[input_length++] = key;
        input_text[input_length] = '\0';
    }
}

int terminal_take_close_request(void)
{
    int requested = close_requested != 0u;
    close_requested = 0u;
    return requested;
}

/* Caractere `index` do texto exibido: historico seguido da linha em edicao. */
static char shown_char(uint32_t index, uint8_t *color)
{
    if (index < history_length) {
        *color = history_color[index];
        return history_text[index];
    }
    *color = COLOR_INPUT;
    return input_text[index - history_length];
}

static void clamp_scroll(void)
{
    if (scroll_offset > scroll_max) {
        scroll_offset = scroll_max;
    }
}

static void draw_arrow(int32_t x, int32_t y, int up)
{
    for (int32_t line = 0; line < 3; ++line) {
        int32_t half = up ? line : 2 - line;
        video_fill_rect(x + 3 - half, y + line, (uint32_t)(2 * half + 1),
                        1u, 15u);
    }
}

/* Barra de rolagem na borda direita: seta para cima, trilho com alca
 * arrastavel e seta para baixo. */
static void draw_scrollbar(int32_t x, int32_t y, uint32_t width,
                           uint32_t height, uint32_t rows,
                           uint32_t max_offset)
{
    int32_t bar_x = (int32_t)width - SCROLLBAR_WIDTH;
    int32_t bar_height = (int32_t)height;
    scroll_x = bar_x;
    scroll_area_height = bar_height;
    scroll_rows = rows;
    scroll_max = max_offset;
    scroll_track_y = SCROLLBAR_BUTTON;
    scroll_track_height = bar_height - 2 * SCROLLBAR_BUTTON;
    if (scroll_track_height < 4) {
        return;
    }

    video_fill_rect(x + bar_x, y, (uint32_t)SCROLLBAR_WIDTH, height, 1u);
    video_draw_hline(x + bar_x, y, (uint32_t)SCROLLBAR_WIDTH, 8u);
    video_fill_rect(x + bar_x, y, 1u, height, 8u);

    video_draw_panel(x + bar_x + 1, y + 1, 7u, 8u, 7u, 8u, 1u);
    draw_arrow(x + bar_x + 2, y + 3, 1);
    video_draw_panel(x + bar_x + 1, y + bar_height - 9, 7u, 8u, 7u, 8u, 1u);
    draw_arrow(x + bar_x + 2, y + bar_height - 7, 0);

    int32_t thumb_height = scroll_track_height;
    int32_t thumb_y = scroll_track_y;
    if (max_offset != 0u) {
        uint32_t total = max_offset + rows;
        thumb_height = (int32_t)((uint32_t)scroll_track_height * rows / total);
        if (thumb_height < 10) {
            thumb_height = 10;
        }
        if (thumb_height > scroll_track_height) {
            thumb_height = scroll_track_height;
        }
        thumb_y = scroll_track_y +
                  (int32_t)(((uint32_t)(scroll_track_height - thumb_height) *
                             (max_offset - scroll_offset)) / max_offset);
    }
    scroll_thumb_y = thumb_y;
    scroll_thumb_height = thumb_height;
    video_draw_panel(x + bar_x + 1, y + thumb_y, 7u, (uint32_t)thumb_height,
                     max_offset != 0u ? 9u : 8u, 2u, 2u);
}

/* Coloca a alca com o topo em `top` (relativo a area) e ajusta a rolagem. */
static void drag_thumb_to(int32_t top)
{
    int32_t span = scroll_track_height - scroll_thumb_height;
    if (scroll_max == 0u || span <= 0) {
        scroll_offset = 0u;
        return;
    }
    int32_t position = top - scroll_track_y;
    if (position < 0) {
        position = 0;
    }
    if (position > span) {
        position = span;
    }
    scroll_offset = scroll_max -
                    ((uint32_t)position * scroll_max) / (uint32_t)span;
    clamp_scroll();
}

void terminal_handle_click(int32_t x, int32_t y)
{
    if (x < scroll_x || scroll_max == 0u) {
        return;
    }
    if (y < scroll_track_y) {
        scroll_offset += SCROLL_STEP;
    } else if (y >= scroll_area_height - SCROLLBAR_BUTTON) {
        scroll_offset = scroll_offset > SCROLL_STEP
            ? scroll_offset - SCROLL_STEP : 0u;
    } else if (y >= scroll_thumb_y &&
               y < scroll_thumb_y + scroll_thumb_height) {
        scroll_dragging = 1u;
        scroll_grab = y - scroll_thumb_y;
    } else if (y < scroll_thumb_y) {
        scroll_offset += scroll_rows > 1u ? scroll_rows - 1u : 1u;
    } else {
        uint32_t page = scroll_rows > 1u ? scroll_rows - 1u : 1u;
        scroll_offset = scroll_offset > page ? scroll_offset - page : 0u;
    }
    clamp_scroll();
}

void terminal_handle_pointer(int32_t x, int32_t y, uint32_t buttons)
{
    (void)x;
    if ((buttons & 1u) == 0u) {
        scroll_dragging = 0u;
        return;
    }
    if (scroll_dragging != 0u) {
        drag_thumb_to(y - scroll_grab);
    }
}

void terminal_draw(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    video_fill_rect(x, y, width, height, COLOR_BACKGROUND);

    uint32_t text_width = width > (uint32_t)SCROLLBAR_WIDTH
        ? width - (uint32_t)SCROLLBAR_WIDTH : width;
    uint32_t columns = text_width > 2u * TERMINAL_PADDING_X + TERMINAL_CELL_WIDTH
        ? (text_width - 2u * TERMINAL_PADDING_X) / TERMINAL_CELL_WIDTH : 1u;
    uint32_t rows = height > 2u * TERMINAL_PADDING_Y + TERMINAL_ROW_HEIGHT
        ? (height - 2u * TERMINAL_PADDING_Y) / TERMINAL_ROW_HEIGHT : 1u;
    uint32_t total = history_length + input_length;

    /* Primeira passada: quantas linhas visuais o texto ocupa (a ultima
     * inclui o cursor, por isso o +1 no fim). */
    uint32_t total_rows = 1u;
    uint32_t column = 0u;
    for (uint32_t index = 0u; index < total; ++index) {
        uint8_t color;
        char character = shown_char(index, &color);
        if (character == '\n') {
            ++total_rows;
            column = 0u;
            continue;
        }
        if (column == columns) {
            ++total_rows;
            column = 0u;
        }
        ++column;
    }
    if (column == columns) {
        ++total_rows;
        column = 0u;
    }
    uint32_t max_offset = total_rows > rows ? total_rows - rows : 0u;
    if (scroll_offset > max_offset) {
        scroll_offset = max_offset;
    }
    uint32_t first_row = max_offset - scroll_offset;

    /* Segunda passada: desenha so as linhas visiveis. */
    uint32_t row = 0u;
    column = 0u;
    for (uint32_t index = 0u; index < total; ++index) {
        uint8_t color;
        char character = shown_char(index, &color);
        if (character == '\n') {
            ++row;
            column = 0u;
            continue;
        }
        if (column == columns) {
            ++row;
            column = 0u;
        }
        if (row >= first_row && row < first_row + rows && character != ' ') {
            video_draw_char_cell(
                x + TERMINAL_PADDING_X + (int32_t)column * TERMINAL_CELL_WIDTH,
                y + TERMINAL_PADDING_Y +
                    (int32_t)(row - first_row) * TERMINAL_ROW_HEIGHT,
                character, color);
        }
        ++column;
    }
    if (column == columns) {
        ++row;
        column = 0u;
    }

    /* Cursor: bloco fino logo apos o texto digitado. */
    if (row >= first_row && row < first_row + rows) {
        video_fill_rect(
            x + TERMINAL_PADDING_X + (int32_t)column * TERMINAL_CELL_WIDTH,
            y + TERMINAL_PADDING_Y +
                (int32_t)(row - first_row) * TERMINAL_ROW_HEIGHT,
            5u, 8u, COLOR_INPUT);
    }

    draw_scrollbar(x, y, width, height, rows, max_offset);
}
