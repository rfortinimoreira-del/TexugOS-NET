#ifndef TEXUGOS_PIC_H
#define TEXUGOS_PIC_H

#include <stdint.h>

void pic_initialize(void);
void pic_unmask_irq(uint32_t irq);
void pic_send_eoi(uint32_t irq);
uint32_t pic_enter_bios_mode(void);
void pic_leave_bios_mode(uint32_t flags);

#endif
