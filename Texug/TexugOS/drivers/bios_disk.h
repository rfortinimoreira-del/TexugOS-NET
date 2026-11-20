#ifndef TEXUGOS_BIOS_DISK_H
#define TEXUGOS_BIOS_DISK_H

#include <stdint.h>

int bios_disk_initialize(void);
uint32_t bios_disk_sector_count(void);
int bios_disk_read_sector(uint32_t lba, uint8_t *buffer);
int bios_disk_write_sector(uint32_t lba, const uint8_t *buffer);

#endif
