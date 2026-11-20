#ifndef TEXUGOS_AHCI_H
#define TEXUGOS_AHCI_H

#include <stdint.h>

int ahci_initialize(void);
const char *ahci_model(void);
uint32_t ahci_sector_count(void);
int ahci_read_sector(uint32_t lba, uint8_t *buffer);
int ahci_write_sector(uint32_t lba, const uint8_t *buffer);

#endif
