#include <stdint.h>

#include "pit.h"

#define PIT_INPUT_HZ 1193180u
#define PIT_CHANNEL_0 0x40u
#define PIT_CHANNEL_2 0x42u
#define PIT_COMMAND 0x43u
#define PIT_SPEAKER 0x61u

static volatile uint32_t ticks;

static void out8(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void pit_initialize(uint32_t frequency_hz)
{
    if (frequency_hz == 0u) {
        frequency_hz = 100u;
    }

    uint32_t divisor = PIT_INPUT_HZ / frequency_hz;
    if (divisor == 0u || divisor > 0xffffu) {
        divisor = 0xffffu;
    }

    out8(PIT_COMMAND, 0x36u);
    out8(PIT_CHANNEL_0, (uint8_t)(divisor & 0xffu));
    out8(PIT_CHANNEL_0, (uint8_t)(divisor >> 8));
}

void pit_handle_interrupt(void)
{
    ++ticks;
}

uint32_t pit_ticks_read(void)
{
    return ticks;
}

void pit_beep(uint32_t frequency_hz, uint32_t duration_ticks)
{
    if (frequency_hz == 0u || duration_ticks == 0u) {
        return;
    }

    uint32_t divisor = PIT_INPUT_HZ / frequency_hz;
    if (divisor == 0u) {
        divisor = 1u;
    } else if (divisor > 0xffffu) {
        divisor = 0xffffu;
    }

    out8(PIT_COMMAND, 0xb6u);
    out8(PIT_CHANNEL_2, (uint8_t)(divisor & 0xffu));
    out8(PIT_CHANNEL_2, (uint8_t)(divisor >> 8));
    out8(PIT_SPEAKER, (uint8_t)(in8(PIT_SPEAKER) | 0x03u));

    uint32_t end_tick = pit_ticks_read() + duration_ticks;
    while ((int32_t)(pit_ticks_read() - end_tick) < 0) {
        __asm__ volatile ("hlt");
    }

    out8(PIT_SPEAKER, (uint8_t)(in8(PIT_SPEAKER) & (uint8_t)~0x03u));
}
