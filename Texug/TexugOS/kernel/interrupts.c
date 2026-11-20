#include <stdint.h>

#include "console.h"
#include "interrupts.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "mouse.h"
#include "threads.h"
#include "user_mode.h"
#include "paging.h"

#define IDT_ENTRIES 48u
#define IDT_STORAGE_ENTRIES 129u
#define KERNEL_CODE_SELECTOR 0x08u
#define TSS_SELECTOR 0x28u
#define INTERRUPT_GATE 0x8eu
#define USER_INTERRUPT_GATE 0xeeu

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t reserved;
    uint8_t flags;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t previous;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

struct interrupt_frame {
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t saved_esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t vector;
    uint32_t error_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
};

static struct idt_entry idt[IDT_STORAGE_ENTRIES];
static uint64_t gdt[6];
static struct tss_entry tss;
static uint8_t privilege_stack[8192] __attribute__((aligned(16)));
extern const uint32_t interrupt_stub_table[IDT_ENTRIES];
extern void user_syscall_stub(void);
extern void load_gdt(const struct idt_pointer *pointer);

static void set_tss_descriptor(uint32_t index, uint32_t base, uint32_t limit)
{
    gdt[index] = (limit & 0xffffu) |
                 ((uint64_t)(base & 0x00ffffffu) << 16) |
                 ((uint64_t)0x89u << 40) |
                 ((uint64_t)((limit >> 16) & 0x0fu) << 48) |
                 ((uint64_t)((base >> 24) & 0xffu) << 56);
}

static void idt_set_gate(uint32_t vector, uint32_t handler, uint8_t flags)
{
    idt[vector].offset_low = (uint16_t)(handler & 0xffffu);
    idt[vector].selector = KERNEL_CODE_SELECTOR;
    idt[vector].reserved = 0;
    idt[vector].flags = flags;
    idt[vector].offset_high = (uint16_t)(handler >> 16);
}

void interrupts_initialize(void)
{
    gdt[0] = 0u;
    gdt[1] = 0x00cf9a000000ffffull;
    gdt[2] = 0x00cf92000000ffffull;
    gdt[3] = 0x00cffa000000ffffull;
    gdt[4] = 0x00cff2000000ffffull;

    for (uint32_t index = 0u; index < sizeof(tss); ++index) {
        ((uint8_t *)&tss)[index] = 0u;
    }
    tss.esp0 = (uint32_t)(uintptr_t)(privilege_stack + sizeof(privilege_stack));
    tss.ss0 = 0x10u;
    tss.iomap_base = sizeof(tss);
    set_tss_descriptor(5u, (uint32_t)(uintptr_t)&tss,
                       sizeof(tss) - 1u);
    const struct idt_pointer gdt_pointer = {
        .limit = (uint16_t)(sizeof(gdt) - 1u),
        .base = (uint32_t)(uintptr_t)gdt,
    };
    load_gdt(&gdt_pointer);
    uint16_t tss_selector = TSS_SELECTOR;
    __asm__ volatile ("ltr %0" : : "r"(tss_selector));

    for (uint32_t vector = 0; vector < IDT_ENTRIES; ++vector) {
        idt_set_gate(vector, interrupt_stub_table[vector], INTERRUPT_GATE);
    }
    idt_set_gate(128u, (uint32_t)(uintptr_t)user_syscall_stub,
                 USER_INTERRUPT_GATE);

    const struct idt_pointer pointer = {
        .limit = (uint16_t)(sizeof(idt) - 1u),
        .base = (uint32_t)(uintptr_t)idt,
    };
    __asm__ volatile ("lidt %0" : : "m"(pointer));
}

void interrupt_dispatch(uint32_t *raw_frame)
{
    struct interrupt_frame *frame = (struct interrupt_frame *)raw_frame;

    if (frame->vector == 128u) {
        frame->eax = user_mode_handle_syscall(frame->eax, frame->ebx);
        return;
    }
    if (frame->vector < 32u && (frame->cs & 3u) == 3u) {
        user_mode_abort(frame->vector, frame->error_code);
    }

    if (frame->vector == 32u) {
        pit_handle_interrupt();
        pic_send_eoi(0u);
        thread_preempt();
        return;
    }
    if (frame->vector == 33u) {
        keyboard_handle_interrupt();
        pic_send_eoi(1u);
        return;
    }
    if (frame->vector == 44u) {
        mouse_handle_interrupt();
        pic_send_eoi(12u);
        return;
    }

    if (frame->vector >= 32u && frame->vector < IDT_ENTRIES) {
        pic_send_eoi(frame->vector - 32u);
        return;
    }

    __asm__ volatile ("cli");
    console_write("CPU exception vector ");
    console_write_u32(frame->vector);
    console_write(", error ");
    console_write_u32(frame->error_code);
    console_write(", EIP 0x");
    console_write_hex32(frame->eip);
    console_write(", CS 0x");
    console_write_hex32(frame->cs);
    console_write(", EAX 0x");
    console_write_hex32(frame->eax);
    console_write(", ECX 0x");
    console_write_hex32(frame->ecx);
    console_write(", EDX 0x");
    console_write_hex32(frame->edx);
    if (frame->vector == 14u) {
        console_write(", CR2 0x");
        console_write_hex32(paging_fault_address());
    }
    console_write("\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
