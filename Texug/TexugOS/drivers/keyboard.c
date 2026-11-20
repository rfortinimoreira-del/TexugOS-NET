#include <stdint.h>

#include "keyboard.h"
#include "pic.h"

#define KEYBOARD_DATA 0x60u
#define KEY_QUEUE_SIZE 128u
#define KEY_QUEUE_MASK (KEY_QUEUE_SIZE - 1u)

static volatile char key_queue[KEY_QUEUE_SIZE];
static volatile uint8_t queue_read;
static volatile uint8_t queue_write;
static uint8_t left_shift;
static uint8_t right_shift;
static uint8_t caps_lock;
static uint8_t caps_key_down;
static uint8_t extended_scancode;

static const char keymap[128] = {
    [0x01] = 0x1b,
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
    [0x0e] = '\b', [0x0f] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']',
    [0x1c] = '\n',
    [0x1e] = 'a', [0x1f] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2b] = '\\',
    [0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c', [0x2f] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/', [0x39] = ' ',
};

static int is_letter(char character)
{
    return character >= 'a' && character <= 'z';
}

static char shifted_character(char character)
{
    switch (character) {
    case '1': return '!';
    case '2': return '@';
    case '3': return '#';
    case '4': return '$';
    case '5': return '%';
    case '6': return '^';
    case '7': return '&';
    case '8': return '*';
    case '9': return '(';
    case '0': return ')';
    case '-': return '_';
    case '=': return '+';
    case '[': return '{';
    case ']': return '}';
    case ';': return ':';
    case '\'': return '"';
    case '`': return '~';
    case '\\': return '|';
    case ',': return '<';
    case '.': return '>';
    case '/': return '?';
    default: return character;
    }
}

void keyboard_initialize(void)
{
    queue_read = 0u;
    queue_write = 0u;
    left_shift = 0u;
    right_shift = 0u;
    caps_lock = 0u;
    caps_key_down = 0u;
    extended_scancode = 0u;
    pic_unmask_irq(1u);
}

void keyboard_handle_interrupt(void)
{
    uint8_t scancode;
    __asm__ volatile ("inb %1, %0" : "=a"(scancode) : "Nd"(KEYBOARD_DATA));
    if (scancode == 0xe0u) {
        extended_scancode = 1u;
        return;
    }
    if (extended_scancode != 0u) {
        extended_scancode = 0u;
        return;
    }

    uint8_t released = scancode & 0x80u;
    uint8_t key = scancode & 0x7fu;
    if (key == 0x2au) {
        left_shift = released == 0u;
        return;
    }
    if (key == 0x36u) {
        right_shift = released == 0u;
        return;
    }
    if (key == 0x3au) {
        if (released != 0u) {
            caps_key_down = 0u;
        } else if (caps_key_down == 0u) {
            caps_lock ^= 1u;
            caps_key_down = 1u;
        }
        return;
    }
    if (released != 0u) {
        return;
    }

    char character = keymap[key];
    if (character == '\0') {
        return;
    }
    uint8_t shift = left_shift | right_shift;
    if (is_letter(character)) {
        if ((shift ^ caps_lock) != 0u) {
            character = (char)(character - 'a' + 'A');
        }
    } else if (shift != 0u) {
        character = shifted_character(character);
    }

    uint8_t next = (uint8_t)((queue_write + 1u) & KEY_QUEUE_MASK);
    if (next != queue_read) {
        key_queue[queue_write] = character;
        queue_write = next;
    }
}

int keyboard_read_character(char *character)
{
    if (character == 0 || queue_read == queue_write) {
        return 0;
    }
    *character = key_queue[queue_read];
    queue_read = (uint8_t)((queue_read + 1u) & KEY_QUEUE_MASK);
    return 1;
}
