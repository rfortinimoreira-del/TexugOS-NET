#include <stdint.h>

#include "apps.h"
#include "arena.h"
#include "ata.h"
#include "fat32.h"
#include "frame_allocator.h"
#include "mouse.h"
#include "network.h"
#include "paint.h"
#include "pci.h"
#include "pit.h"
#include "snake.h"
#include "terminal.h"
#include "tetris.h"
#include "video.h"

#define APP_COUNT 9u
#define FILE_CAPACITY 64u
#define FILE_PREVIEW_SIZE 256u
#define NOTEPAD_NAME_SIZE 48u
#define NOTEPAD_TEXT_SIZE 512u
#define STATUS_TEXT_SIZE 28u
#define BROWSER_RESPONSE_SIZE 768u
#define BROWSER_LINE_COUNT 6u
#define BROWSER_LINE_SIZE 28u
#define BROWSER_INPUT_SIZE 64u

static uint32_t files_directory;
static uint32_t files_selected;
static uint8_t files_preview_open;
static char files_preview[FILE_PREVIEW_SIZE + 1u];
static uint32_t files_preview_length;
static struct fat32_entry file_entries[FILE_CAPACITY];
static uint32_t file_entry_count;
static uint8_t file_list_valid;
static uint8_t file_list_dirty;

static char notepad_name[NOTEPAD_NAME_SIZE];
static uint32_t notepad_name_length;
static char notepad_text[NOTEPAD_TEXT_SIZE];
static uint32_t notepad_text_length;
static uint8_t notepad_name_focused;
static char notepad_status[STATUS_TEXT_SIZE];
static uint8_t notepad_status_color;

static char browser_input[BROWSER_INPUT_SIZE];
static uint32_t browser_input_length;
static char browser_lines[BROWSER_LINE_COUNT][BROWSER_LINE_SIZE];
static uint32_t browser_line_count;
static char browser_status[BROWSER_LINE_SIZE];
static uint8_t browser_status_color;
static uint8_t mouse_speed_fast;
static uint8_t settings_dhcp_attempted;

static uint32_t text_length(const char *text)
{
    uint32_t length = 0u;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static void text_copy(char *destination, const char *source,
                      uint32_t capacity)
{
    if (capacity == 0u) {
        return;
    }
    uint32_t index = 0u;
    while (index + 1u < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static void append_u32(char *buffer, uint32_t value)
{
    char digits[10];
    uint32_t count = 0u;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);

    uint32_t length = text_length(buffer);
    while (count != 0u && length + 1u < STATUS_TEXT_SIZE) {
        buffer[length++] = digits[--count];
    }
    buffer[length] = '\0';
}

static void append_ipv4(char buffer[STATUS_TEXT_SIZE],
                        const uint8_t address[4])
{
    buffer[0] = '\0';
    for (uint32_t index = 0u; index < 4u; ++index) {
        if (index != 0u) {
            uint32_t length = text_length(buffer);
            if (length + 1u < STATUS_TEXT_SIZE) {
                buffer[length++] = '.';
                buffer[length] = '\0';
            }
        }
        append_u32(buffer, address[index]);
    }
}

static int text_starts_with(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (*text++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

static int parse_browser_target(const char *text, char host[64],
                                uint16_t *port, char path[96])
{
    const char *cursor = text;
    static const char http_prefix[] = "http://";
    if (text_starts_with(cursor, http_prefix)) {
        cursor += sizeof(http_prefix) - 1u;
    }
    uint32_t host_length = 0u;
    while (*cursor != '\0' && *cursor != ':' && *cursor != '/') {
        char character = *cursor++;
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '.' || character == '-') ||
            host_length + 1u >= 64u) {
            return 0;
        }
        host[host_length++] = character;
    }
    if (host_length == 0u) {
        return 0;
    }
    host[host_length] = '\0';

    *port = 80u;
    if (*cursor == ':') {
        ++cursor;
        uint32_t value = 0u;
        uint32_t digits = 0u;
        while (*cursor >= '0' && *cursor <= '9') {
            value = value * 10u + (uint32_t)(*cursor++ - '0');
            if (value > 65535u || ++digits > 5u) {
                return 0;
            }
        }
        if (digits == 0u || value == 0u) {
            return 0;
        }
        *port = (uint16_t)value;
    }

    if (*cursor == '\0') {
        path[0] = '/';
        path[1] = '\0';
        return 1;
    }
    if (*cursor != '/') {
        return 0;
    }
    text_copy(path, cursor, 96u);
    return text_length(path) == text_length(cursor);
}

static int parse_browser_ipv4(const char *host, uint8_t address[4])
{
    const char *cursor = host;
    for (uint32_t part = 0u; part < 4u; ++part) {
        uint32_t value = 0u;
        uint32_t digits = 0u;
        while (*cursor >= '0' && *cursor <= '9') {
            value = value * 10u + (uint32_t)(*cursor++ - '0');
            if (value > 255u || ++digits > 3u) {
                return 0;
            }
        }
        if (digits == 0u || (part < 3u ? *cursor++ != '.' : *cursor != '\0')) {
            return 0;
        }
        address[part] = (uint8_t)value;
    }
    return 1;
}

static void browser_add_character(char character, uint32_t *line_length,
                                  char line[BROWSER_LINE_SIZE])
{
    if (*line_length + 1u >= BROWSER_LINE_SIZE ||
        browser_line_count == BROWSER_LINE_COUNT) {
        return;
    }
    line[(*line_length)++] = character;
}

static void browser_flush_line(char line[BROWSER_LINE_SIZE],
                               uint32_t *line_length)
{
    if (browser_line_count == BROWSER_LINE_COUNT) {
        *line_length = 0u;
        return;
    }
    line[*line_length] = '\0';
    text_copy(browser_lines[browser_line_count++], line,
              BROWSER_LINE_SIZE);
    *line_length = 0u;
}

static int browser_render_response(const char *response, uint32_t length)
{
    uint32_t body = 0u;
    while (body + 3u < length) {
        if (response[body] == '\r' && response[body + 1u] == '\n' &&
            response[body + 2u] == '\r' && response[body + 3u] == '\n') {
            body += 4u;
            break;
        }
        ++body;
    }
    if (body >= length) {
        return 0;
    }

    uint8_t inside_tag = 0u;
    char line[BROWSER_LINE_SIZE];
    uint32_t line_length = 0u;
    for (uint32_t index = body; index < length; ++index) {
        char character = response[index];
        if (character == '<') {
            inside_tag = 1u;
            continue;
        }
        if (character == '>') {
            inside_tag = 0u;
            browser_add_character(' ', &line_length, line);
            continue;
        }
        if (inside_tag) {
            continue;
        }
        if (character == '\r') {
            continue;
        }
        if (character == '\n') {
            browser_flush_line(line, &line_length);
            if (browser_line_count == BROWSER_LINE_COUNT) {
                break;
            }
        } else if (character == ' ' || character == '\t') {
            if (line_length != 0u &&
                line[line_length - 1u] != ' ') {
                browser_add_character(' ', &line_length, line);
            }
        } else if (character >= ' ' && character <= '~') {
            browser_add_character(character, &line_length, line);
        }
    }
    if (line_length != 0u && browser_line_count < BROWSER_LINE_COUNT) {
        browser_flush_line(line, &line_length);
    }
    return 1;
}

static void browser_load_page(void)
{
    uint8_t address[4];
    uint16_t port;
    char host[64];
    char path[96];
    browser_line_count = 0u;
    if (!parse_browser_target(browser_input, host, &port, path)) {
        text_copy(browser_status, "INVALID HTTP URL", sizeof(browser_status));
        browser_status_color = 4u;
        return;
    }
    struct network_config config;
    network_get_config(&config);
    if (config.configured == 0u) {
        text_copy(browser_status, "RUN DHCP FIRST", sizeof(browser_status));
        browser_status_color = 14u;
        return;
    }
    if (!parse_browser_ipv4(host, address) &&
        !network_resolve_ipv4(host, address)) {
        text_copy(browser_status, "DNS LOOKUP FAILED",
                  sizeof(browser_status));
        browser_status_color = 12u;
        return;
    }

    char response[BROWSER_RESPONSE_SIZE];
    uint32_t response_length = 0u;
    if (!network_http_get(address, port, host, path, response,
                          sizeof(response), &response_length)) {
        text_copy(browser_status, "HTTP REQUEST FAILED",
                  sizeof(browser_status));
        browser_status_color = 5u;
        return;
    }
    if (!browser_render_response(response, response_length)) {
        text_copy(browser_status, "INVALID HTTP RESPONSE",
                  sizeof(browser_status));
        browser_status_color = 4u;
        return;
    }
    text_copy(browser_status, "HTTP RESPONSE RECEIVED",
              sizeof(browser_status));
    browser_status_color = 10u;
}

static void draw_browser(int32_t x, int32_t y, uint32_t width,
                         uint32_t height)
{
    /* Barra de titulo e barra de estado: lisas, escuras, com borda. */
    video_draw_panel(x + 3, y + 3, width - 6u, 11u, 1u, 2u, 2u);
    video_fill_rect(x + 6, y + 7, 3u, 3u, browser_status_color);
    video_draw_text_middle(x + 13, y + 3, 11u, "HTTP Browser", 15u);
    video_draw_panel(x + 3, y + 16, width - 6u, 11u, 1u, 2u, 2u);
    video_draw_text_middle(x + 7, y + 16, 11u, browser_status,
                           browser_status_color);
    for (uint32_t row = 0u; row < browser_line_count; ++row) {
        video_draw_text(x + 5, y + 30 + (int32_t)row * 10,
                        browser_lines[row], 1u);
    }

    /* Campo de endereco: branco com borda. */
    uint32_t field_width = width - 43u;
    int32_t control_y = y + (int32_t)height - 16;
    video_draw_panel(x + 4, control_y, field_width, 14u, 15u, 8u, 2u);
    video_draw_text_middle(x + 7, control_y, 14u, ">", 9u);
    uint32_t input_start = 0u;
    while (input_start < browser_input_length &&
           video_text_width(browser_input + input_start) + 20u >
               field_width) {
        ++input_start;
    }
    video_draw_text_middle(x + 16, control_y, 14u,
                           browser_input + input_start, 1u);
    video_draw_button(x + (int32_t)width - 36, control_y,
                      32u, 14u, "GO", 9u, 15u);
}

static void refresh_file_list(void)
{
    file_list_valid = (uint8_t)fat32_list(files_directory, file_entries,
                                          FILE_CAPACITY, &file_entry_count);
    file_list_dirty = 0u;
    if (files_selected >= file_entry_count) {
        files_selected = 0u;
    }
}

static void files_go_up(void)
{
    if (files_directory != fat32_root_cluster()) {
        struct fat32_entry parent;
        if (fat32_find(files_directory, "..", &parent)) {
            files_directory = parent.first_cluster;
            files_selected = 0u;
            file_list_dirty = 1u;
        }
    }
}

static void files_go_home(void)
{
    files_directory = fat32_root_cluster();
    files_selected = 0u;
    file_list_dirty = 1u;
}

static void files_activate_selected(void)
{
    if (file_list_dirty != 0u) {
        refresh_file_list();
    }
    if (!file_list_valid || files_selected >= file_entry_count) {
        return;
    }
    struct fat32_entry *entry = &file_entries[files_selected];
    if ((entry->attributes & FAT32_ATTRIBUTE_DIRECTORY) != 0u) {
        files_directory = entry->first_cluster;
        files_selected = 0u;
        file_list_dirty = 1u;
    } else if (fat32_read_file(files_directory, entry->name, files_preview,
                               FILE_PREVIEW_SIZE, &files_preview_length)) {
        files_preview[files_preview_length] = '\0';
        files_preview_open = 1u;
    }
}

static void draw_files(int32_t x, int32_t y, uint32_t width,
                       uint32_t height)
{
    if (files_preview_open) {
        video_draw_button(x + 4, y + 2, 34u, 13u, "BACK", 2u, 15u);
        video_draw_text(x + 43, y + 5, "FILE PREVIEW", 1u);
        if (files_preview_length == 0u) {
            video_draw_text(x + 5, y + 18, "EMPTY FILE", 1u);
        } else {
            char line[27];
            uint32_t line_length = 0u;
            int32_t line_y = y + 18;
            for (uint32_t index = 0u; index < files_preview_length &&
                 line_y + 7 < y + (int32_t)height; ++index) {
                char character = files_preview[index];
                if (character == '\n' || line_length == sizeof(line) - 1u) {
                    line[line_length] = '\0';
                    video_draw_text(x + 5, line_y, line, 1u);
                    line_y += 10;
                    line_length = 0u;
                    if (character == '\n') {
                        continue;
                    }
                }
                if (character >= ' ' && character <= '~') {
                    line[line_length++] = character;
                }
            }
            if (line_length != 0u && line_y + 7 < y + (int32_t)height) {
                line[line_length] = '\0';
                video_draw_text(x + 5, line_y, line, 1u);
            }
        }
        return;
    }

    if (file_list_dirty != 0u) {
        refresh_file_list();
    }
    video_draw_button(x + 4, y + 2, 24u, 13u, "UP", 2u, 15u);
    video_draw_button(x + 32, y + 2, 34u, 13u, "OPEN", 9u, 15u);
    video_draw_button(x + 70, y + 2, 36u, 13u, "HOME", 2u, 15u);
    video_draw_text(x + 112, y + 5, "FILES", 1u);
    if (!file_list_valid) {
        video_draw_text(x + 5, y + 18, "DIRECTORY LIST FAILED", 4u);
        return;
    }
    if (file_entry_count == 0u) {
        video_draw_text(x + 5, y + 18, "EMPTY DIRECTORY", 1u);
        return;
    }

    uint32_t visible = height > 18u ? (height - 18u) / 10u : 0u;
    if (visible > file_entry_count) {
        visible = file_entry_count;
    }
    for (uint32_t row = 0u; row < visible; ++row) {
        uint32_t index = row;
        if (index == files_selected) {
            video_fill_rect(x + 3, y + 16 + (int32_t)row * 10,
                            width - 10u, 10u, 9u);
        }
        video_draw_text(x + 5, y + 18 + (int32_t)row * 10,
                        file_entries[index].name,
                        index == files_selected ? 15u : 1u);
        if ((file_entries[index].attributes &
             FAT32_ATTRIBUTE_DIRECTORY) != 0u) {
            video_draw_text(x + 5 + (int32_t)video_text_width(
                                file_entries[index].name),
                            y + 18 + (int32_t)row * 10, "/",
                            index == files_selected ? 15u : 1u);
        }
    }
}

static void notepad_set_status(const char *text, uint8_t color)
{
    text_copy(notepad_status, text, sizeof(notepad_status));
    notepad_status_color = color;
}

static void notepad_save(void)
{
    if (ata_storage_read_only()) {
        notepad_set_status("STORAGE READ ONLY", 4u);
        return;
    }
    if (notepad_name[0] == '\0') {
        notepad_set_status("ENTER A FILE NAME", 4u);
        return;
    }

    struct fat32_entry existing;
    if (fat32_find(files_directory, notepad_name, &existing)) {
        notepad_set_status("FILE ALREADY EXISTS", 4u);
        return;
    }
    if (!fat32_write_file(files_directory, notepad_name, notepad_text,
                          notepad_text_length)) {
        notepad_set_status("SAVE FAILED", 4u);
        return;
    }
    file_list_dirty = 1u;
    notepad_set_status("FILE SAVED", 10u);
}

static void notepad_append(char character)
{
    if (notepad_text_length + 1u >= sizeof(notepad_text)) {
        notepad_set_status("DOCUMENT FULL", 4u);
        return;
    }
    notepad_text[notepad_text_length++] = character;
    notepad_text[notepad_text_length] = '\0';
    notepad_set_status("UNSAVED CHANGES", 14u);
}

static void notepad_handle_key(char key)
{
    if (key == '\t') {
        notepad_name_focused ^= 1u;
        notepad_set_status(notepad_name_focused ? "TYPE FILE NAME"
                                                : "UNSAVED CHANGES",
                           notepad_name_focused ? 11u : 14u);
        return;
    }
    if (notepad_name_focused != 0u) {
        if (key == '\n') {
            notepad_name_focused = 0u;
            notepad_set_status("UNSAVED CHANGES", 14u);
        } else if (key == '\b') {
            if (notepad_name[0] != '\0') {
                notepad_name[--notepad_name_length] = '\0';
                notepad_set_status("TYPE FILE NAME", 11u);
            }
        } else if (key >= ' ' && key <= '~' &&
                   notepad_name_length + 1u < sizeof(notepad_name)) {
            notepad_name[notepad_name_length++] = key;
            notepad_name[notepad_name_length] = '\0';
            notepad_set_status("PRESS ENTER TO WRITE", 11u);
        }
        return;
    }
    if (key == '\b') {
        if (notepad_text_length != 0u) {
            notepad_text[--notepad_text_length] = '\0';
            notepad_set_status("UNSAVED CHANGES", 14u);
        }
    } else if (key == '\n' || (key >= ' ' && key <= '~')) {
        notepad_append(key);
    }
}

static void draw_notepad(int32_t x, int32_t y, uint32_t width,
                         uint32_t height)
{
    int32_t toolbar_y = y + 3;
    uint32_t field_width = width - 78u;
    video_draw_panel(x + 4, toolbar_y, field_width, 14u, 15u,
                     notepad_name_focused ? 9u : 8u, 2u);
    video_draw_text_middle(x + 7, toolbar_y, 14u, "File", 8u);
    uint32_t name_start = 0u;
    uint32_t name_length = text_length(notepad_name);
    while (name_start < name_length &&
           video_text_width(notepad_name + name_start) + 36u >
               field_width) {
        ++name_start;
    }
    video_draw_text_middle(x + 34, toolbar_y, 14u,
                           notepad_name[0] == '\0' ? "NEWFILE.TXT"
                                                   : notepad_name + name_start,
                           notepad_name[0] == '\0' ? 8u : 1u);
    video_draw_button(x + (int32_t)width - 36, toolbar_y,
                      32u, 14u, "SAVE", 9u, 15u);

    int32_t editor_y = y + 22;
    int32_t footer_y = y + (int32_t)height - 15;
    uint32_t editor_height = footer_y > editor_y
        ? (uint32_t)(footer_y - editor_y) : 0u;
    video_fill_rect(x + 4, editor_y, width - 8u, editor_height, 15u);
    video_draw_frame(x + 4, editor_y, width - 8u, editor_height, 8u);

    uint32_t columns = width > 12u ? (width - 12u) / 6u : 1u;
    uint32_t visible_rows = editor_height > 4u
        ? (editor_height - 4u) / 9u : 1u;
    uint32_t total_rows = 1u;
    uint32_t column = 0u;
    for (uint32_t index = 0u; index < notepad_text_length; ++index) {
        if (notepad_text[index] == '\n' || column == columns) {
            ++total_rows;
            column = 0u;
            if (notepad_text[index] == '\n') {
                continue;
            }
        }
        ++column;
    }
    uint32_t first_row = total_rows > visible_rows
        ? total_rows - visible_rows : 0u;
    uint32_t row = 0u;
    column = 0u;
    for (uint32_t index = 0u; index < notepad_text_length; ++index) {
        char character = notepad_text[index];
        if (character == '\n') {
            ++row;
            column = 0u;
            continue;
        }
        if (column == columns) {
            ++row;
            column = 0u;
        }
        if (character >= ' ' && character <= '~' &&
            row >= first_row && row < first_row + visible_rows) {
            video_draw_char_cell(x + 6 + (int32_t)column * 6,
                                 editor_y + 3 +
                                     (int32_t)(row - first_row) * 9,
                                 character, 1u);
        }
        ++column;
    }
    if (notepad_name_focused == 0u) {
        uint32_t cursor_row = total_rows - 1u;
        uint32_t cursor_column = column;
        if (cursor_row >= first_row &&
            cursor_row < first_row + visible_rows &&
            cursor_column < columns) {
            video_fill_rect(x + 6 + (int32_t)cursor_column * 6,
                            editor_y + 3 +
                                (int32_t)(cursor_row - first_row) * 9,
                            1u, 7u, 9u);
        }
    }

    video_draw_panel(x + 4, footer_y, width - 8u, 12u, 1u, 2u, 2u);
    video_draw_text_middle(x + 7, footer_y, 12u, notepad_status,
                           notepad_status_color);
}

static void draw_settings(int32_t x, int32_t y, uint32_t width,
                          uint32_t height)
{
    video_draw_text(x + 5, y + 5, "SYSTEM STATUS", 1u);
    video_draw_text(x + 5, y + 18,
                    video_display_width() == VIDEO_VBE_WIDTH
                        ? "DISPLAY 640 X 480" : "DISPLAY 320 X 200",
                    1u);
    video_draw_text(x + 5, y + 29, "BOOT LEGACY BIOS", 1u);
    video_draw_text(x + 5, y + 40, "FREE FRAMES", 1u);
    char value[STATUS_TEXT_SIZE] = "";
    append_u32(value, frame_free_count());
    video_draw_text(x + 88, y + 40, value, 1u);
    struct pci_device wireless_device;
    video_draw_text(x + 5, y + 51,
                    pci_find_device(0x10ecu, 0x8171u, &wireless_device)
                        ? "RTL8191SE SUPPORT INCOMPLETE"
                        : "WI-FI ADAPTER NOT FOUND",
                    12u);
    struct network_config config;
    struct pci_device ethernet_device;
    int rtl810x_detected =
        pci_find_device(0x10ecu, 0x8136u, &ethernet_device);
    network_get_config(&config);
    const char *ethernet_status = !network_is_available()
        ? (rtl810x_detected ? "RTL810x DRIVER MISSING"
                            : "NO SUPPORTED ETHERNET")
        : (config.configured != 0u ? "RTL8139 CONNECTED"
                                   : "RTL8139 DETECTED");
    video_draw_text(x + 5, y + 62, ethernet_status,
                    network_is_available() ? 10u : 12u);
    if (config.configured != 0u) {
        append_ipv4(value, config.address);
        video_draw_text(x + 5, y + 73, "IP", 1u);
        video_draw_text(x + 23, y + 73, value, 1u);
    } else if (settings_dhcp_attempted != 0u) {
        video_draw_text(x + 5, y + 73, network_dhcp_error(), 12u);
    } else {
        video_draw_text(x + 5, y + 73, "IP: NOT CONFIGURED", 8u);
    }
    uint32_t button_width = width > 104u ? 96u : width - 8u;
    video_draw_button(x + 4, y + (int32_t)height - 34, button_width, 14u,
                      config.configured != 0u ? "RENEW IP" : "CONNECT WIRED",
                      9u, 15u);
    video_draw_button(x + 4, y + (int32_t)height - 16, button_width, 14u,
                      mouse_speed_fast ? "MOUSE FAST" : "MOUSE NORMAL",
                      mouse_speed_fast ? 10u : 9u, 15u);
}

void desktop_apps_initialize(void)
{
    files_directory = fat32_root_cluster();
    files_selected = 0u;
    files_preview_open = 0u;
    files_preview_length = 0u;
    notepad_name[0] = '\0';
    notepad_name_length = 0u;
    notepad_text[0] = '\0';
    notepad_text_length = 0u;
    notepad_name_focused = 1u;
    notepad_set_status("ENTER NAME, THEN TYPE", 11u);
    file_entry_count = 0u;
    file_list_valid = 1u;
    file_list_dirty = 1u;
    arena_initialize();
    snake_initialize();
    tetris_initialize();
    paint_initialize();
    terminal_initialize();
    browser_input_length = 0u;
    browser_input[0] = '\0';
    browser_line_count = 0u;
    mouse_speed_fast = 0u;
    text_copy(browser_status, "ENTER HOST:PORT/PATH",
              sizeof(browser_status));
    browser_status_color = 4u;
    settings_dhcp_attempted = 0u;
}

void desktop_app_draw(uint32_t index, int32_t x, int32_t y,
                      uint32_t width, uint32_t height)
{
    if (index >= APP_COUNT) {
        return;
    }
    if (index == 0u) {
        draw_files(x, y, width, height);
    } else if (index == 1u) {
        draw_settings(x, y, width, height);
    } else if (index == 2u) {
        draw_browser(x, y, width, height);
    } else if (index == 3u) {
        arena_draw(x, y, width, height);
    } else if (index == 4u) {
        draw_notepad(x, y, width, height);
    } else if (index == 5u) {
        snake_draw(x, y, width, height);
    } else if (index == 6u) {
        tetris_draw(x, y, width, height);
    } else if (index == 7u) {
        paint_draw(x, y, width, height);
    } else {
        terminal_draw(x, y, width, height);
    }
}

void desktop_app_handle_key(uint32_t index, char key)
{
    if (index == 4u) {
        notepad_handle_key(key);
        return;
    }
    if (index == 3u) {
        arena_handle_key(key);
        return;
    }
    if (index == 8u) {
        terminal_handle_key(key);
        return;
    }
    if (index == 0u) {
        if (key == '\b') {
            if (files_preview_open) {
                files_preview_open = 0u;
            } else {
                files_go_up();
            }
        } else if (key == '\n' && !files_preview_open) {
            files_activate_selected();
        }
        return;
    }
    if (index == 5u) {
        snake_handle_key(key);
        return;
    }
    if (index == 6u) {
        tetris_handle_key(key);
        return;
    }

    if (index == 2u) {
        if (key == '\n') {
            browser_load_page();
        } else if (key == '\b') {
            if (browser_input_length != 0u) {
                browser_input[--browser_input_length] = '\0';
            }
        } else if (key >= ' ' && key <= '~' &&
                   browser_input_length + 1u < BROWSER_INPUT_SIZE) {
            browser_input[browser_input_length++] = key;
            browser_input[browser_input_length] = '\0';
        }
        return;
    }
}

void desktop_app_handle_mouse(uint32_t index, int32_t delta_x)
{
    if (index == 3u) {
        arena_handle_mouse(delta_x);
    }
}

void desktop_app_handle_pointer(uint32_t index, int32_t x, int32_t y,
                                uint32_t buttons)
{
    if (index == 7u) {
        paint_handle_pointer(x, y, buttons);
    } else if (index == 8u) {
        terminal_handle_pointer(x, y, buttons);
    }
}

void desktop_app_handle_click(uint32_t index, int32_t x, int32_t y,
                              uint32_t window_width,
                              uint32_t window_height)
{
    if (index == 3u) {
        arena_fire();
        return;
    }
    if (index == 4u) {
        if (y >= 20 && y < 34) {
            if (x >= (int32_t)window_width - 39 &&
                x < (int32_t)window_width - 7) {
                notepad_save();
            } else if (x >= 7 &&
                       x < (int32_t)window_width - 77) {
                notepad_name_focused = 1u;
                notepad_set_status("TYPE FILE NAME", 11u);
            }
        } else if (y >= 39 &&
                   y < (int32_t)window_height - 18) {
            notepad_name_focused = 0u;
            notepad_set_status("UNSAVED CHANGES", 14u);
        }
        return;
    }
    if (index == 8u) {
        terminal_handle_click(x - 3, y - 17);
        return;
    }
    if (index == 7u) {
        paint_handle_click(x - 3, y - 17,
                           window_width - 6u, window_height - 20u);
        return;
    }
    uint32_t body_width = window_width - 6u;
    if (index == 1u) {
        uint32_t button_width = body_width > 104u ? 96u : body_width - 8u;
        if (x >= 7 && x < 7 + (int32_t)button_width &&
            y >= (int32_t)window_height - 39 &&
            y < (int32_t)window_height - 22) {
            settings_dhcp_attempted = 1u;
            (void)network_dhcp();
            return;
        }
        if (x >= 7 && x < 7 + (int32_t)button_width &&
            y >= (int32_t)window_height - 19 &&
            y < (int32_t)window_height - 5) {
            mouse_speed_fast ^= 1u;
        }
        return;
    }
    if (index == 2u) {
        int32_t go_x = (int32_t)body_width - 33;
        if (x >= go_x && x < go_x + 32 &&
            y >= (int32_t)window_height - 19 &&
            y < (int32_t)window_height - 5) {
            browser_load_page();
        }
        return;
    }
    if (index != 0u || x < 0) {
        return;
    }
    if (files_preview_open) {
        if (x >= 7 && x < 41 && y >= 19 && y < 32) {
            files_preview_open = 0u;
        }
        return;
    }
    if (y >= 19 && y < 32) {
        if (x >= 7 && x < 31) {
            files_go_up();
        } else if (x >= 35 && x < 69) {
            files_activate_selected();
        } else if (x >= 73 && x < 109) {
            files_go_home();
        }
        return;
    }
    if (y < 35) {
        return;
    }
    if (file_list_dirty != 0u) {
        refresh_file_list();
    }
    if (!file_list_valid) {
        return;
    }
    uint32_t selected = (uint32_t)(y - 35) / 10u;
    if (selected < file_entry_count) {
        files_selected = selected;
    }
}

uint32_t desktop_app_mouse_sensitivity(void)
{
    return mouse_speed_fast ? 2u : 1u;
}