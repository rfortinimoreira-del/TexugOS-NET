#include <stdint.h>

#include "ahci.h"
#include "ata.h"
#include "bios_disk.h"
#include "console.h"

#ifndef TEXUGOS_BIOS_DISK
#define TEXUGOS_BIOS_DISK 1
#endif

#define ATA_DATA 0x1f0u
#define ATA_SECTOR_COUNT 0x1f2u
#define ATA_LBA_LOW 0x1f3u
#define ATA_LBA_MID 0x1f4u
#define ATA_LBA_HIGH 0x1f5u
#define ATA_DRIVE 0x1f6u
#define ATA_STATUS 0x1f7u
#define ATA_COMMAND 0x1f7u
#define ATA_ALT_STATUS 0x3f6u
#define ATA_STATUS_ERROR 0x01u
#define ATA_STATUS_DEVICE_FAULT 0x20u
#define ATA_STATUS_DRQ 0x08u
#define ATA_STATUS_BUSY 0x80u
#define ATA_TIMEOUT 100000u

static int disk_present;
static uint32_t sector_count;
static char disk_model[41];
static int bios_disk_active;
static int ahci_active;

static void out8(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void out16(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static uint16_t in16(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void io_delay(void)
{
    (void)in8(ATA_ALT_STATUS);
    (void)in8(ATA_ALT_STATUS);
    (void)in8(ATA_ALT_STATUS);
    (void)in8(ATA_ALT_STATUS);
}

static int wait_not_busy(void)
{
    for (uint32_t count = 0; count < ATA_TIMEOUT; ++count) {
        uint8_t status = in8(ATA_STATUS);
        if (status == 0u || status == 0xffu) {
            return 0;
        }
        if ((status & ATA_STATUS_BUSY) != 0u) {
            continue;
        }
        if ((status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) != 0u) {
            return 0;
        }
        return 1;
    }
    return 0;
}

static int wait_for_data(void)
{
    for (uint32_t count = 0; count < ATA_TIMEOUT; ++count) {
        uint8_t status = in8(ATA_STATUS);
        if (status == 0u || status == 0xffu) {
            return 0;
        }
        if ((status & ATA_STATUS_BUSY) != 0u) {
            continue;
        }
        if ((status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) != 0u) {
            return 0;
        }
        if ((status & ATA_STATUS_DRQ) != 0u) {
            return 1;
        }
    }
    return 0;
}

static int wait_complete(void)
{
    for (uint32_t count = 0; count < ATA_TIMEOUT; ++count) {
        uint8_t status = in8(ATA_STATUS);
        if (status == 0u || status == 0xffu) {
            return 0;
        }
        if ((status & ATA_STATUS_BUSY) != 0u) {
            continue;
        }
        if ((status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) != 0u) {
            return 0;
        }
        if ((status & ATA_STATUS_DRQ) == 0u) {
            return 1;
        }
    }
    return 0;
}

static int ata_pio_initialize(void)
{
    uint16_t identify[256];
    out8(ATA_DRIVE, 0xa0u);
    io_delay();
    out8(ATA_SECTOR_COUNT, 0u);
    out8(ATA_LBA_LOW, 0u);
    out8(ATA_LBA_MID, 0u);
    out8(ATA_LBA_HIGH, 0u);
    out8(ATA_COMMAND, 0xecu);

    uint8_t status = in8(ATA_STATUS);
    if (status == 0u || status == 0xffu) {
        return 0;
    }
    if (!wait_not_busy() || in8(ATA_LBA_MID) != 0u ||
        in8(ATA_LBA_HIGH) != 0u || !wait_for_data()) {
        return 0;
    }

    for (uint32_t index = 0; index < 256u; ++index) {
        identify[index] = in16(ATA_DATA);
    }
    if ((identify[49] & 0x0200u) == 0u) {
        return 0;
    }

    sector_count = (uint32_t)identify[60] |
                   ((uint32_t)identify[61] << 16);
    if (sector_count == 0u) {
        return 0;
    }

    for (uint32_t index = 0; index < 20u; ++index) {
        disk_model[index * 2u] = (char)(identify[27u + index] >> 8);
        disk_model[index * 2u + 1u] = (char)identify[27u + index];
    }
    disk_model[40] = '\0';
    for (int32_t end = 39; end >= 0 && disk_model[end] == ' '; --end) {
        disk_model[end] = '\0';
    }

    disk_present = 1;
    return 1;
}

int ata_initialize(void)
{
    bios_disk_active = 0;
    ahci_active = 0;
#if TEXUGOS_BIOS_DISK
    console_write("Storage: probing BIOS boot disk...\n");
    if (bios_disk_initialize()) {
        bios_disk_active = 1;
        disk_present = 0;
        sector_count = 0u;
        console_write("Storage: BIOS disk ready\n");
        return 1;
    }
#else
    console_write("Storage: BIOS disk support disabled; skipping EDD probe.\n");
#endif

    console_write("Storage: ATA PIO identify...\n");
    if (ata_pio_initialize()) {
        console_write("Storage: ATA PIO device ready\n");
        return 1;
    }
    console_write("Storage: ATA PIO unavailable; probing AHCI...\n");
    if (ahci_initialize()) {
        ahci_active = 1;
        console_write("Storage: AHCI device ready\n");
        return 1;
    }
    console_write("Storage: no disk interface responded\n");
    return 0;
}

int ata_uses_bios_disk(void)
{
#if TEXUGOS_BIOS_DISK
    return bios_disk_active;
#else
    return 0;
#endif
}

int ata_storage_read_only(void)
{
#if TEXUGOS_BIOS_DISK
    return bios_disk_active;
#else
    return 0;
#endif
}

const char *ata_model(void)
{
#if TEXUGOS_BIOS_DISK
    if (bios_disk_active) {
        return "BIOS boot disk";
    }
#endif
    return ahci_active ? ahci_model() : disk_model;
}

int ata_uses_ahci(void)
{
    return ahci_active;
}

uint32_t ata_sector_count(void)
{
    if (bios_disk_active) {
        return bios_disk_sector_count();
    }
    if (ahci_active) {
        return ahci_sector_count();
    }
    return disk_present ? sector_count : 0u;
}

static int select_sector(uint32_t lba, uint8_t command)
{
    if (!disk_present || lba >= sector_count || lba >= 0x10000000u) {
        return 0;
    }
    if (!wait_complete()) {
        return 0;
    }

    out8(ATA_DRIVE, (uint8_t)(0xe0u | ((lba >> 24) & 0x0fu)));
    io_delay();
    out8(ATA_SECTOR_COUNT, 1u);
    out8(ATA_LBA_LOW, (uint8_t)lba);
    out8(ATA_LBA_MID, (uint8_t)(lba >> 8));
    out8(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    out8(ATA_COMMAND, command);
    if (!wait_for_data()) {
        return 0;
    }
    return 1;
}

int ata_read_sector(uint32_t lba, uint8_t *buffer)
{
    if (bios_disk_active) {
        return bios_disk_read_sector(lba, buffer);
    }
    if (ahci_active) {
        return ahci_read_sector(lba, buffer);
    }
    if (buffer == 0 || !select_sector(lba, 0x20u)) {
        return 0;
    }

    uint16_t *words = (uint16_t *)buffer;
    for (uint32_t index = 0; index < 256u; ++index) {
        words[index] = in16(ATA_DATA);
    }
    return 1;
}

int ata_write_sector(uint32_t lba, const uint8_t *buffer)
{
    if (bios_disk_active) {
        (void)lba;
        (void)buffer;
        return 0;
    }
    if (ahci_active) {
        return ahci_write_sector(lba, buffer);
    }
    if (buffer == 0 || !select_sector(lba, 0x30u)) {
        return 0;
    }

    const uint16_t *words = (const uint16_t *)buffer;
    for (uint32_t index = 0; index < 256u; ++index) {
        out16(ATA_DATA, words[index]);
    }
    if (!wait_complete()) {
        return 0;
    }

    return 1;
}
