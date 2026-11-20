#ifndef TEXUGOS_FRAME_ALLOCATOR_H
#define TEXUGOS_FRAME_ALLOCATOR_H

#include <stdint.h>

#include "e820.h"

#define FRAME_INVALID 0xffffffffu

void frame_allocator_initialize(const volatile struct e820_entry *entries,
                                uint32_t entry_count);
uint32_t frame_allocate(void);
uint32_t frame_allocate_below(uint64_t address_limit);
int frame_free(uint32_t physical_address);
uint32_t frame_free_count(void);

#endif
