#include <stdint.h>

#include "e820.h"
#include "frame_allocator.h"

#define PAGE_SHIFT 12u
#define PAGE_SIZE (1u << PAGE_SHIFT)
#define MAX_FRAMES 1048576u
#define BITMAP_WORDS (MAX_FRAMES / 32u)
#define RESERVED_FRAMES (0x00100000u / PAGE_SIZE)
#define MAX_PHYSICAL_ADDRESS 0x100000000ull

extern uint8_t __kernel_end;

static uint32_t frame_bitmap[BITMAP_WORDS];
static uint32_t usable_bitmap[BITMAP_WORDS];
static uint32_t free_frames;
static uint32_t search_start = RESERVED_FRAMES;

static int frame_is_used(uint32_t frame)
{
    return (frame_bitmap[frame / 32u] & (1u << (frame % 32u))) != 0u;
}

static int frame_is_managed(uint32_t frame)
{
    return (usable_bitmap[frame / 32u] & (1u << (frame % 32u))) != 0u;
}

static void set_frame_used(uint32_t frame, int used)
{
    uint32_t mask = 1u << (frame % 32u);
    if (used) {
        frame_bitmap[frame / 32u] |= mask;
    } else {
        frame_bitmap[frame / 32u] &= ~mask;
    }
}

void frame_allocator_initialize(const volatile struct e820_entry *entries,
                                uint32_t entry_count)
{
    for (uint32_t word = 0; word < BITMAP_WORDS; ++word) {
        frame_bitmap[word] = 0xffffffffu;
        usable_bitmap[word] = 0u;
    }
    free_frames = 0;
    search_start = RESERVED_FRAMES;
    uint32_t kernel_end_frame =
        ((uint32_t)(uintptr_t)&__kernel_end + PAGE_SIZE - 1u) / PAGE_SIZE;

    for (uint32_t index = 0; index < entry_count; ++index) {
        if (entries[index].type != 1u || entries[index].length == 0u ||
            entries[index].base >= MAX_PHYSICAL_ADDRESS) {
            continue;
        }

        uint64_t end = entries[index].base + entries[index].length;
        if (end < entries[index].base || end > MAX_PHYSICAL_ADDRESS) {
            end = MAX_PHYSICAL_ADDRESS;
        }

        uint64_t first = (entries[index].base + PAGE_SIZE - 1u) / PAGE_SIZE;
        uint64_t last = end / PAGE_SIZE;
        if (first < RESERVED_FRAMES) {
            first = RESERVED_FRAMES;
        }
        if (last > MAX_FRAMES) {
            last = MAX_FRAMES;
        }

        for (uint32_t frame = (uint32_t)first; frame < (uint32_t)last; ++frame) {
            if (frame < kernel_end_frame) {
                continue;
            }
            uint32_t mask = 1u << (frame % 32u);
            uint32_t word = frame / 32u;
            usable_bitmap[word] |= mask;
            if ((frame_bitmap[word] & mask) != 0u) {
                frame_bitmap[word] &= ~mask;
                ++free_frames;
            }
        }
    }
}

uint32_t frame_allocate_below(uint64_t address_limit)
{
    uint64_t frame_limit = address_limit >> PAGE_SHIFT;
    if (frame_limit > MAX_FRAMES) {
        frame_limit = MAX_FRAMES;
    }
    if (free_frames == 0u || frame_limit <= RESERVED_FRAMES) {
        return FRAME_INVALID;
    }

    for (uint32_t frame = search_start;
         frame < (uint32_t)frame_limit; ++frame) {
        if (!frame_is_used(frame)) {
            set_frame_used(frame, 1);
            --free_frames;
            search_start = frame + 1u;
            return frame << PAGE_SHIFT;
        }
    }

    uint32_t wrapped_limit = search_start < frame_limit
        ? search_start : (uint32_t)frame_limit;
    for (uint32_t frame = RESERVED_FRAMES; frame < wrapped_limit; ++frame) {
        if (!frame_is_used(frame)) {
            set_frame_used(frame, 1);
            --free_frames;
            search_start = frame + 1u;
            return frame << PAGE_SHIFT;
        }
    }

    return FRAME_INVALID;
}

uint32_t frame_allocate(void)
{
    return frame_allocate_below(MAX_PHYSICAL_ADDRESS);
}

int frame_free(uint32_t physical_address)
{
    if ((physical_address & (PAGE_SIZE - 1u)) != 0u ||
        physical_address < RESERVED_FRAMES * PAGE_SIZE) {
        return 0;
    }

    uint32_t frame = physical_address >> PAGE_SHIFT;
    if (frame >= MAX_FRAMES || !frame_is_managed(frame) ||
        !frame_is_used(frame)) {
        return 0;
    }

    set_frame_used(frame, 0);
    ++free_frames;
    if (frame < search_start) {
        search_start = frame;
    }
    return 1;
}

uint32_t frame_free_count(void)
{
    return free_frames;
}
