#include <stdint.h>

#include "heap.h"

#define HEAP_SIZE (64u * 1024u)
#define HEAP_ALIGNMENT 8u

struct heap_block {
    uint32_t size;
    uint32_t free;
    struct heap_block *previous;
    struct heap_block *next;
};

static uint8_t heap_region[HEAP_SIZE] __attribute__((aligned(16)));
static struct heap_block *first_block;

void heap_initialize(void)
{
    first_block = (struct heap_block *)heap_region;
    first_block->size = HEAP_SIZE - sizeof(*first_block);
    first_block->free = 1u;
    first_block->previous = 0;
    first_block->next = 0;
}

void *kmalloc(uint32_t size)
{
    if (size == 0u || size > HEAP_SIZE - sizeof(struct heap_block)) {
        return 0;
    }

    size = (size + (HEAP_ALIGNMENT - 1u)) & ~(HEAP_ALIGNMENT - 1u);
    for (struct heap_block *block = first_block; block != 0; block = block->next) {
        if (block->free == 0u || block->size < size) {
            continue;
        }

        uint32_t remaining =
            block->size - size - (uint32_t)sizeof(struct heap_block);
        if (remaining >= HEAP_ALIGNMENT) {
            struct heap_block *split =
                (struct heap_block *)((uint8_t *)(block + 1) + size);
            split->size = remaining;
            split->free = 1u;
            split->previous = block;
            split->next = block->next;
            if (split->next != 0) {
                split->next->previous = split;
            }
            block->next = split;
            block->size = size;
        }
        block->free = 0u;
        return block + 1;
    }

    return 0;
}

int kfree(void *pointer)
{
    if (pointer == 0 || (uint8_t *)pointer < heap_region + sizeof(struct heap_block) ||
        (uint8_t *)pointer >= heap_region + HEAP_SIZE) {
        return 0;
    }

    struct heap_block *block = first_block;
    while (block != 0 && (void *)(block + 1) != pointer) {
        block = block->next;
    }
    if (block == 0 || block->free != 0u) {
        return 0;
    }

    block->free = 1u;
    if (block->next != 0 && block->next->free != 0u) {
        struct heap_block *next = block->next;
        block->size += (uint32_t)sizeof(*block) + next->size;
        block->next = next->next;
        if (block->next != 0) {
            block->next->previous = block;
        }
    }

    if (block->previous != 0 && block->previous->free != 0u) {
        struct heap_block *previous = block->previous;
        previous->size += (uint32_t)sizeof(*previous) + block->size;
        previous->next = block->next;
        if (previous->next != 0) {
            previous->next->previous = previous;
        }
    }
    return 1;
}
