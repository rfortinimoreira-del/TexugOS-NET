#include <stdint.h>

#include "mouse.h"
#include "pic.h"

#define PS2_STATUS 0x64u
#define PS2_COMMAND 0x64u
#define PS2_DATA 0x60u
#define PS2_OUTPUT_FULL 0x01u
#define PS2_INPUT_FULL 0x02u
#define PS2_AUXILIARY_DATA 0x20u
#define PS2_TIMEOUT 1000000u
#define SCREEN_WIDTH 640u
#define SCREEN_HEIGHT 200u

static volatile struct mouse_state state = {
    .x = SCREEN_WIDTH / 2u,
    .y = SCREEN_HEIGHT / 2u,
};
static uint8_t packet[3];
static uint8_t packet_index;

static uint8_t in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void out8(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static int wait_input_clear(void)
{
    for (uint32_t count = 0; count < PS2_TIMEOUT; ++count) {
        if ((in8(PS2_STATUS) & PS2_INPUT_FULL) == 0u) {
            return 1;
        }
    }
    return 0;
}

static int wait_output_full(void)
{
    for (uint32_t count = 0; count < PS2_TIMEOUT; ++count) {
        if ((in8(PS2_STATUS) & PS2_OUTPUT_FULL) != 0u) {
            return 1;
        }
    }
    return 0;
}

static int controller_command(uint8_t command)
{
    if (!wait_input_clear()) {
        return 0;
    }
    out8(PS2_COMMAND, command);
    return 1;
}

static int controller_write(uint8_t value)
{
    if (!wait_input_clear()) {
        return 0;
    }
    out8(PS2_DATA, value);
    return 1;
}

static int mouse_command(uint8_t command)
{
    if (!controller_command(0xd4u) || !controller_write(command) ||
        !wait_output_full()) {
        return 0;
    }
    return in8(PS2_DATA) == 0xfau;
}

static int32_t signed_delta(uint8_t value)
{
    return value < 0x80u ? (int32_t)value : (int32_t)value - 256;
}

int mouse_initialize(void)
{
    if (!controller_command(0xa8u) || !controller_command(0x20u) ||
        !wait_output_full()) {
        return 0;
    }
    uint8_t configuration = in8(PS2_DATA);
    configuration |= 0x02u;
    configuration &= (uint8_t)~0x20u;
    if (!controller_command(0x60u) || !controller_write(configuration) ||
        !mouse_command(0xf6u) || !mouse_command(0xf4u)) {
        return 0;
    }

    packet_index = 0u;
    state.packets = 0u;
    state.buttons = 0u;
    state.x = SCREEN_WIDTH / 2u;
    state.y = SCREEN_HEIGHT / 2u;
    pic_unmask_irq(12u);
    return 1;
}

void mouse_handle_interrupt(void)
{
    uint8_t status = in8(PS2_STATUS);
    if ((status & PS2_OUTPUT_FULL) == 0u) {
        return;
    }
    uint8_t value = in8(PS2_DATA);
    if ((status & PS2_AUXILIARY_DATA) == 0u) {
        return;
    }

    if (packet_index == 0u && (value & 0x08u) == 0u) {
        return;
    }
    packet[packet_index++] = value;
    if (packet_index != sizeof(packet)) {
        return;
    }
    packet_index = 0u;
    if ((packet[0] & 0xc0u) != 0u) {
        return;
    }

    int32_t x = (int32_t)state.x + signed_delta(packet[1]);
    int32_t y = (int32_t)state.y - signed_delta(packet[2]);
    if (x < 0) {
        x = 0;
    } else if (x >= (int32_t)SCREEN_WIDTH) {
        x = (int32_t)SCREEN_WIDTH - 1;
    }
    if (y < 0) {
        y = 0;
    } else if (y >= (int32_t)SCREEN_HEIGHT) {
        y = (int32_t)SCREEN_HEIGHT - 1;
    }

    state.x = (uint32_t)x;
    state.y = (uint32_t)y;
    state.buttons = packet[0] & 0x07u;
    ++state.packets;
}

void mouse_get_state(struct mouse_state *output)
{
    if (output == 0) {
        return;
    }
    output->packets = state.packets;
    output->x = state.x;
    output->y = state.y;
    output->buttons = state.buttons;
}
