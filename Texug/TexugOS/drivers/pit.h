#ifndef TEXUGOS_PIT_H
#define TEXUGOS_PIT_H

#include <stdint.h>

void pit_initialize(uint32_t frequency_hz);
void pit_handle_interrupt(void);
uint32_t pit_ticks_read(void);
void pit_beep(uint32_t frequency_hz, uint32_t duration_ticks);

#endif
