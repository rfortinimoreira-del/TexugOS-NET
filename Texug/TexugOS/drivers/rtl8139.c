#include <stdint.h>

#include "pci.h"
#include "rtl8139.h"

#define RTL8139_VENDOR 0x10ecu
#define RTL8139_DEVICE 0x8139u
#define RTL8139_RX_SIZE 8192u
#define RTL8139_RX_MASK (RTL8139_RX_SIZE - 1u)
#define RTL8139_FRAME_MAX 1536u
#define RTL8139_TIMEOUT 1000000u

#define REG_MAC 0x00u
#define REG_TX_STATUS 0x10u
#define REG_TX_ADDRESS 0x20u
#define REG_RX_BUFFER 0x30u
#define REG_COMMAND 0x37u
#define REG_RX_READ 0x38u
#define REG_INTERRUPT_MASK 0x3cu
#define REG_INTERRUPT_STATUS 0x3eu
#define REG_TX_CONFIG 0x40u
#define REG_RX_CONFIG 0x44u
#define REG_CONFIG1 0x52u

#define COMMAND_RX_ENABLE 0x08u
#define COMMAND_TX_ENABLE 0x04u
#define COMMAND_RESET 0x10u
#define COMMAND_RX_EMPTY 0x01u
#define TX_STATUS_OWNED 0x2000u
#define TX_STATUS_OK 0x8000u

static uint16_t io_base;
static uint16_t receive_offset;
static uint8_t transmit_index;
static uint8_t available;
static uint8_t mac_address[6];
static volatile uint8_t receive_buffer[RTL8139_RX_SIZE + 16u]
    __attribute__((aligned(256)));
static uint8_t transmit_buffers[4][RTL8139_FRAME_MAX]
    __attribute__((aligned(16)));

static void out8(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static void out16(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static void out32(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static uint8_t in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

static uint32_t in32(uint16_t port)
{
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       uint32_t length)
{
    for (uint32_t index = 0u; index < length; ++index) {
        destination[index] = source[index];
    }
}

int rtl8139_initialize(void)
{
    available = 0u;
    struct pci_device device;
    if (!pci_find_device(RTL8139_VENDOR, RTL8139_DEVICE, &device)) {
        return 0;
    }

    uint32_t bar = pci_read_config32(device.bus, device.slot,
                                     device.function, 0x10u);
    if ((bar & 1u) == 0u) {
        return 0;
    }
    io_base = (uint16_t)(bar & 0xfffcu);
    if (io_base == 0u) {
        return 0;
    }

    uint32_t command = pci_read_config32(device.bus, device.slot,
                                         device.function, 0x04u);
    pci_write_config16(device.bus, device.slot, device.function, 0x04u,
                       (uint16_t)(command | 0x0005u));

    out8(io_base + REG_CONFIG1, 0u);
    out8(io_base + REG_COMMAND, COMMAND_RESET);
    uint32_t attempts = RTL8139_TIMEOUT;
    while ((in8(io_base + REG_COMMAND) & COMMAND_RESET) != 0u &&
           --attempts != 0u) {
    }
    if (attempts == 0u) {
        return 0;
    }

    for (uint32_t index = 0u; index < sizeof(mac_address); ++index) {
        mac_address[index] = in8(io_base + REG_MAC + (uint16_t)index);
    }
    out32(io_base + REG_RX_BUFFER, (uint32_t)(uintptr_t)receive_buffer);
    for (uint32_t index = 0u; index < 4u; ++index) {
        out32(io_base + REG_TX_ADDRESS + index * 4u,
              (uint32_t)(uintptr_t)transmit_buffers[index]);
    }
    out16(io_base + REG_RX_READ, 0xfff0u);
    out16(io_base + REG_INTERRUPT_MASK, 0u);
    out16(io_base + REG_INTERRUPT_STATUS, 0xffffu);
    out32(io_base + REG_TX_CONFIG, 0x03000700u);
    out32(io_base + REG_RX_CONFIG, 0x0000008fu);
    out8(io_base + REG_COMMAND, COMMAND_RX_ENABLE | COMMAND_TX_ENABLE);

    receive_offset = 0u;
    transmit_index = 0u;
    available = 1u;
    return 1;
}

int rtl8139_is_available(void)
{
    return available != 0u;
}

int rtl8139_send(const void *frame, uint16_t length)
{
    if (available == 0u || frame == 0 || length < 14u ||
        length > RTL8139_FRAME_MAX) {
        return 0;
    }

    uint8_t index = transmit_index;
    uint16_t status_port = (uint16_t)(io_base + REG_TX_STATUS + index * 4u);
    uint32_t attempts = RTL8139_TIMEOUT;
    uint32_t status = in32(status_port);
    while ((status & TX_STATUS_OWNED) == 0u && --attempts != 0u) {
        status = in32(status_port);
    }
    if (attempts == 0u) {
        return 0;
    }

    copy_bytes(transmit_buffers[index], frame, length);
    out32(status_port, length);
    attempts = RTL8139_TIMEOUT;
    do {
        status = in32(status_port);
        if ((status & TX_STATUS_OWNED) != 0u) {
            transmit_index = (uint8_t)((index + 1u) & 3u);
            return (status & TX_STATUS_OK) != 0u;
        }
    } while (--attempts != 0u);
    return 0;
}

int rtl8139_receive(void *frame, uint16_t capacity, uint16_t *length)
{
    if (available == 0u || frame == 0 || length == 0 ||
        (in8(io_base + REG_COMMAND) & COMMAND_RX_EMPTY) != 0u) {
        return 0;
    }

    uint16_t packet_status = (uint16_t)(
        receive_buffer[receive_offset] |
        ((uint16_t)receive_buffer[receive_offset + 1u] << 8));
    uint16_t packet_size = (uint16_t)(
        receive_buffer[receive_offset + 2u] |
        ((uint16_t)receive_buffer[receive_offset + 3u] << 8));
    if (packet_size < 4u || packet_size > RTL8139_FRAME_MAX + 4u) {
        receive_offset = 0u;
        out16(io_base + REG_RX_READ, (uint16_t)(receive_offset - 16u));
        return 0;
    }

    uint16_t frame_size = (uint16_t)(packet_size - 4u);
    if ((packet_status & 1u) != 0u && frame_size <= capacity) {
        uint32_t start = (receive_offset + 4u) & RTL8139_RX_MASK;
        uint8_t *destination = (uint8_t *)frame;
        for (uint32_t index = 0u; index < frame_size; ++index) {
            destination[index] =
                receive_buffer[(start + index) & RTL8139_RX_MASK];
        }
        *length = frame_size;
    } else {
        frame_size = 0u;
    }

    receive_offset = (uint16_t)((receive_offset + packet_size + 7u) & ~3u);
    receive_offset &= RTL8139_RX_MASK;
    out16(io_base + REG_RX_READ, (uint16_t)(receive_offset - 16u));
    return frame_size != 0u;
}

void rtl8139_get_mac(uint8_t address[6])
{
    if (address == 0) {
        return;
    }
    for (uint32_t index = 0u; index < 6u; ++index) {
        address[index] = mac_address[index];
    }
}
