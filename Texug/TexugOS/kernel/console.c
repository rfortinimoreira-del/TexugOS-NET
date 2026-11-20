#include <stdint.h>

#include "console.h"

#define VGA_MEMORY ((volatile uint16_t *)0xb8000u)
#define VGA_WIDTH 80u
#define VGA_HEIGHT 25u
#define VGA_COLOR 0x0fu

static uint32_t cursor;

static void scroll_if_needed(void)
{
    if (cursor < VGA_WIDTH * VGA_HEIGHT) {
        return;
    }

    for (uint32_t index = 0; index < VGA_WIDTH * (VGA_HEIGHT - 1u); ++index) {
        VGA_MEMORY[index] = VGA_MEMORY[index + VGA_WIDTH];
    }
    for (uint32_t index = VGA_WIDTH * (VGA_HEIGHT - 1u);
         index < VGA_WIDTH * VGA_HEIGHT; ++index) {
        VGA_MEMORY[index] = (uint16_t)((VGA_COLOR << 8) | ' ');
    }
    cursor = VGA_WIDTH * (VGA_HEIGHT - 1u);
}

static void put_character(char character)
{
    if (character == '\n') {
        cursor = ((cursor / VGA_WIDTH) + 1u) * VGA_WIDTH;
        scroll_if_needed();
        return;
    }
    if (character == '\b') {
        if (cursor != 0u) {
            --cursor;
            VGA_MEMORY[cursor] = (uint16_t)((VGA_COLOR << 8) | ' ');
        }
        return;
    }

    scroll_if_needed();
    VGA_MEMORY[cursor++] = (uint16_t)((VGA_COLOR << 8) | (uint8_t)character);
}

void console_clear(void)
{
    for (uint32_t index = 0; index < VGA_WIDTH * VGA_HEIGHT; ++index) {
        VGA_MEMORY[index] = (uint16_t)((VGA_COLOR << 8) | ' ');
    }
    cursor = 0;
}

void console_write(const char *text)
{
    while (*text != '\0') {
        put_character(*text++);
    }
}

void console_write_character(char character)
{
    put_character(character);
}

void console_write_hex32(uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    for (int32_t shift = 28; shift >= 0; shift -= 4) {
        put_character(digits[(value >> shift) & 0x0fu]);
    }
}

void console_write_u32(uint32_t value)
{
    char digits[10];
    uint32_t length = 0;

    do {
        digits[length++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);

    while (length != 0u) {
        put_character(digits[--length]);
    }
}
