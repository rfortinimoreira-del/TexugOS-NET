#ifndef TEXUGOS_CONSOLE_H
#define TEXUGOS_CONSOLE_H

#include <stdint.h>

/* Desvia toda a saida do console (console_write*, console_clear) para as
 * funcoes dadas, por exemplo para a janela do app Terminal. Chamar com
 * (0, 0) restaura a tela de texto VGA. */
void console_redirect(void (*put)(char), void (*clear)(void));

void console_clear(void);
void console_write(const char *text);
void console_write_character(char character);
void console_write_hex32(uint32_t value);
void console_write_u32(uint32_t value);

#endif
