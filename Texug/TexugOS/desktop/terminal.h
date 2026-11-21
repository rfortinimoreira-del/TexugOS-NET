#ifndef TEXUGOS_TERMINAL_H
#define TEXUGOS_TERMINAL_H

#include <stdint.h>

/* App Terminal do desktop: janela com o mesmo shell da tela de boot
 * (mesmos comandos) mais o comando `texugo`. */
void terminal_initialize(void);
void terminal_draw(int32_t x, int32_t y, uint32_t width, uint32_t height);
void terminal_handle_key(char key);

/* Barra de rolagem: coordenadas relativas a area interna da janela. */
void terminal_handle_click(int32_t x, int32_t y);
void terminal_handle_pointer(int32_t x, int32_t y, uint32_t buttons);

/* Retorna 1 (uma unica vez) depois que o usuario digitou `exit`; o desktop
 * entao fecha a janela do Terminal. */
int terminal_take_close_request(void);

#endif
