#include <stdint.h>

#include "pic.h"

#define PIC_MASTER_COMMAND 0x20u
#define PIC_MASTER_DATA 0x21u
#define PIC_SLAVE_COMMAND 0xa0u
#define PIC_SLAVE_DATA 0xa1u

static void out8(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void io_wait(void)
{
    out8(0x80u, 0u);
}

static void remap(uint8_t master_offset, uint8_t slave_offset)
{
    out8(PIC_MASTER_COMMAND, 0x11u);
    io_wait();
    out8(PIC_SLAVE_COMMAND, 0x11u);
    io_wait();
    out8(PIC_MASTER_DATA, master_offset);
    io_wait();
    out8(PIC_SLAVE_DATA, slave_offset);
    io_wait();
    out8(PIC_MASTER_DATA, 0x04u);
    io_wait();
    out8(PIC_SLAVE_DATA, 0x02u);
    io_wait();
    out8(PIC_MASTER_DATA, 0x01u);
    io_wait();
    out8(PIC_SLAVE_DATA, 0x01u);
    io_wait();
}

static uint8_t saved_master_mask;
static uint8_t saved_slave_mask;
static uint8_t bios_master_mask;
static uint8_t bios_slave_mask;
static int initialized;

void pic_initialize(void)
{
    __asm__ volatile ("inb %1, %0" : "=a"(bios_master_mask)
                     : "Nd"(PIC_MASTER_DATA));
    __asm__ volatile ("inb %1, %0" : "=a"(bios_slave_mask)
                     : "Nd"(PIC_SLAVE_DATA));

    remap(0x20u, 0x28u);

    out8(PIC_MASTER_DATA, 0xffu);
    out8(PIC_SLAVE_DATA, 0xffu);
    initialized = 1;
}

uint32_t pic_enter_bios_mode(void)
{
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    if (initialized) {
        __asm__ volatile ("inb %1, %0" : "=a"(saved_master_mask)
                         : "Nd"(PIC_MASTER_DATA));
        __asm__ volatile ("inb %1, %0" : "=a"(saved_slave_mask)
                         : "Nd"(PIC_SLAVE_DATA));
        remap(0x08u, 0x70u);
        out8(PIC_MASTER_DATA, bios_master_mask);
        out8(PIC_SLAVE_DATA, bios_slave_mask);
    }
    return flags;
}

void pic_leave_bios_mode(uint32_t flags)
{
    if (initialized) {
        remap(0x20u, 0x28u);
        out8(PIC_MASTER_DATA, saved_master_mask);
        out8(PIC_SLAVE_DATA, saved_slave_mask);
    }
    if ((flags & 0x00000200u) != 0u) {
        __asm__ volatile ("sti" : : : "memory");
    }
}

void pic_send_eoi(uint32_t irq)
{
    if (irq >= 8u) {
        out8(PIC_SLAVE_COMMAND, 0x20u);
    }
    out8(PIC_MASTER_COMMAND, 0x20u);
}

void pic_unmask_irq(uint32_t irq)
{
    if (irq < 8u) {
        uint8_t mask;
        __asm__ volatile ("inb %1, %0" : "=a"(mask) : "Nd"(PIC_MASTER_DATA));
        out8(PIC_MASTER_DATA, (uint8_t)(mask & ~(1u << irq)));
    } else if (irq < 16u) {
        uint8_t mask;
        __asm__ volatile ("inb %1, %0" : "=a"(mask) : "Nd"(PIC_SLAVE_DATA));
        out8(PIC_SLAVE_DATA, (uint8_t)(mask & ~(1u << (irq - 8u))));
        pic_unmask_irq(2u);
    }
}
