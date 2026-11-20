#ifndef TEXUGOS_CONSOLE_H
#define TEXUGOS_CONSOLE_H

#include <stdint.h>

void console_clear(void);
void console_write(const char *text);
void console_write_character(char character);
void console_write_hex32(uint32_t value);
void console_write_u32(uint32_t value);

#endif
