#ifndef TEXUGOS_HEAP_H
#define TEXUGOS_HEAP_H

#include <stdint.h>

void heap_initialize(void);
void *kmalloc(uint32_t size);
int kfree(void *pointer);

#endif
