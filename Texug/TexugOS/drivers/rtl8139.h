#ifndef TEXUGOS_RTL8139_H
#define TEXUGOS_RTL8139_H

#include <stdint.h>

int rtl8139_initialize(void);
int rtl8139_is_available(void);
int rtl8139_send(const void *frame, uint16_t length);
int rtl8139_receive(void *frame, uint16_t capacity, uint16_t *length);
void rtl8139_get_mac(uint8_t address[6]);

#endif
