#ifndef TEXUGOS_MOUSE_H
#define TEXUGOS_MOUSE_H

#include <stdint.h>

struct mouse_state {
    uint32_t packets;
    uint32_t x;
    uint32_t y;
    uint8_t buttons;
};

int mouse_initialize(void);
void mouse_handle_interrupt(void);
void mouse_get_state(struct mouse_state *state);

#endif
