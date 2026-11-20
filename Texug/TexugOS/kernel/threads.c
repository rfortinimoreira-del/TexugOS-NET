#include <stdint.h>

#include "threads.h"

#define MAX_THREADS 8u
#define THREAD_STACK_SIZE 4096u

enum thread_state {
    THREAD_UNUSED,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_DEAD
};

struct thread {
    uint32_t *stack_pointer;
    thread_entry_t entry;
    void *argument;
    enum thread_state state;
};

static struct thread threads[MAX_THREADS];
static uint8_t thread_stacks[MAX_THREADS][THREAD_STACK_SIZE]
    __attribute__((aligned(16)));
static uint32_t *scheduler_stack;
static int32_t current_thread = -1;
static uint32_t completed_threads;
static uint32_t scheduler_cursor;

extern void context_switch(uint32_t **old_stack, uint32_t *new_stack);

static void thread_bootstrap(void) __attribute__((noreturn));

static int32_t scheduler_next_ready(void)
{
    for (uint32_t offset = 0u; offset < MAX_THREADS; ++offset) {
        uint32_t index = (scheduler_cursor + offset) % MAX_THREADS;
        if (threads[index].state == THREAD_READY) {
            scheduler_cursor = (index + 1u) % MAX_THREADS;
            return (int32_t)index;
        }
    }
    return -1;
}

static void thread_bootstrap(void)
{
    struct thread *thread = &threads[(uint32_t)current_thread];
    __asm__ volatile ("sti");
    thread->entry(thread->argument);
    __asm__ volatile ("cli");
    thread->state = THREAD_DEAD;
    ++completed_threads;
    context_switch(&thread->stack_pointer, scheduler_stack);

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

int thread_create(thread_entry_t entry, void *argument)
{
    if (entry == 0 || current_thread >= 0) {
        return -1;
    }

    for (uint32_t index = 0; index < MAX_THREADS; ++index) {
        if (threads[index].state != THREAD_UNUSED) {
            continue;
        }

        uintptr_t top =
            (uintptr_t)&thread_stacks[index][THREAD_STACK_SIZE] - sizeof(uint32_t);
        uint32_t *stack = (uint32_t *)top;
        *--stack = (uint32_t)(uintptr_t)thread_bootstrap;
        *--stack = 0u;
        *--stack = 0u;
        *--stack = 0u;
        *--stack = 0u;

        threads[index].stack_pointer = stack;
        threads[index].entry = entry;
        threads[index].argument = argument;
        threads[index].state = THREAD_READY;
        return (int)index;
    }

    return -1;
}

void thread_yield(void)
{
    if (current_thread < 0) {
        return;
    }

    __asm__ volatile ("cli");
    struct thread *thread = &threads[(uint32_t)current_thread];
    thread->state = THREAD_READY;
    context_switch(&thread->stack_pointer, scheduler_stack);
    __asm__ volatile ("sti");
}

void thread_preempt(void)
{
    if (current_thread < 0) {
        return;
    }

    int32_t next_thread = scheduler_next_ready();
    if (next_thread < 0) {
        return;
    }

    struct thread *current = &threads[(uint32_t)current_thread];
    current->state = THREAD_READY;
    threads[(uint32_t)next_thread].state = THREAD_RUNNING;
    current_thread = next_thread;
    context_switch(&current->stack_pointer,
                   threads[(uint32_t)next_thread].stack_pointer);
}

unsigned int scheduler_run(void)
{
    completed_threads = 0;
    scheduler_cursor = 0;

    for (;;) {
        __asm__ volatile ("cli");
        int32_t next_thread = scheduler_next_ready();
        if (next_thread < 0) {
            __asm__ volatile ("sti");
            break;
        }

        current_thread = next_thread;
        threads[(uint32_t)current_thread].state = THREAD_RUNNING;
        context_switch(&scheduler_stack,
                       threads[(uint32_t)current_thread].stack_pointer);
        current_thread = -1;
        __asm__ volatile ("sti");
    }

    return completed_threads;
}
