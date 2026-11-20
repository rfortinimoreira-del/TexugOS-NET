#ifndef TEXUGOS_E820_H
#define TEXUGOS_E820_H

#include <stdint.h>

struct e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attributes;
} __attribute__((packed));

#endif
