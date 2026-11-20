#include <stdint.h>

#include "bios_disk.h"
#include "console.h"
#include "pic.h"

#define BIOS_DISK_OPERATION_PROBE 1u
#define BIOS_DISK_OPERATION_PARAMETERS 2u
#define BIOS_DISK_OPERATION_READ 3u
#define BIOS_DISK_OPERATION_WRITE 4u
#define BIOS_DISK_SECTOR_SIZE 512u
#define BIOS_DISK_DAP_ADDRESS 0x6000u
#define BIOS_DISK_BUFFER_ADDRESS 0x6100u
#define BIOS_DISK_PARAMETERS_ADDRESS 0x6400u

struct bios_disk_address_packet {
    uint8_t size;
    uint8_t reserved;
    uint16_t sector_count;
    uint16_t buffer_offset;
    uint16_t buffer_segment;
    uint64_t lba;
} __attribute__((packed));

static uint32_t total_sectors;

extern int bios_int13_call(uint32_t operation);

static int call_bios(uint32_t operation)
{
    uint32_t flags = pic_enter_bios_mode();
    int result = bios_int13_call(operation);
    pic_leave_bios_mode(flags);
    return result;
}

static uint64_t read_u64(volatile const uint8_t *bytes)
{
    uint64_t value = 0u;
    for (uint32_t index = 0u; index < 8u; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return value;
}

int bios_disk_initialize(void)
{
    total_sectors = 0u;
    volatile struct bios_disk_address_packet *packet =
        (volatile struct bios_disk_address_packet *)BIOS_DISK_DAP_ADDRESS;
    volatile uint8_t *parameters =
        (volatile uint8_t *)BIOS_DISK_PARAMETERS_ADDRESS;
    packet->size = sizeof(*packet);
    packet->reserved = 0u;
    packet->sector_count = 1u;
    packet->buffer_offset = BIOS_DISK_BUFFER_ADDRESS;
    packet->buffer_segment = 0u;
    packet->lba = 0u;

    console_write("Storage: BIOS EDD probe...\n");
    if (!call_bios(BIOS_DISK_OPERATION_PROBE)) {
        console_write("Storage: BIOS EDD probe failed\n");
        return 0;
    }

    console_write("Storage: BIOS EDD probe passed; reading parameters...\n");
    for (uint32_t index = 0u; index < 30u; ++index) {
        parameters[index] = 0u;
    }
    *(volatile uint16_t *)parameters = 30u;
    if (!call_bios(BIOS_DISK_OPERATION_PARAMETERS) ||
        parameters[24] != 0x00u || parameters[25] != 0x02u) {
        console_write("Storage: BIOS EDD parameters unavailable\n");
        return 0;
    }

    uint64_t sectors = read_u64(&parameters[16]);
    if (sectors == 0u) {
        console_write("Storage: BIOS reported an empty disk\n");
        return 0;
    }
    total_sectors = sectors > UINT32_MAX ? UINT32_MAX : (uint32_t)sectors;
    console_write("Storage: BIOS EDD parameters accepted\n");
    return 1;
}

uint32_t bios_disk_sector_count(void)
{
    return total_sectors;
}

static int transfer_sector(uint32_t lba, uint32_t operation)
{
    if (total_sectors == 0u || lba >= total_sectors) {
        return 0;
    }

    volatile struct bios_disk_address_packet *packet =
        (volatile struct bios_disk_address_packet *)BIOS_DISK_DAP_ADDRESS;
    packet->size = sizeof(*packet);
    packet->reserved = 0u;
    packet->sector_count = 1u;
    packet->buffer_offset = BIOS_DISK_BUFFER_ADDRESS;
    packet->buffer_segment = 0u;
    packet->lba = lba;
    return call_bios(operation);
}

int bios_disk_read_sector(uint32_t lba, uint8_t *buffer)
{
    if (buffer == 0 || !transfer_sector(lba, BIOS_DISK_OPERATION_READ)) {
        return 0;
    }
    volatile const uint8_t *source =
        (volatile const uint8_t *)BIOS_DISK_BUFFER_ADDRESS;
    for (uint32_t index = 0u; index < BIOS_DISK_SECTOR_SIZE; ++index) {
        buffer[index] = source[index];
    }
    return 1;
}

int bios_disk_write_sector(uint32_t lba, const uint8_t *buffer)
{
    if (buffer == 0) {
        return 0;
    }
    volatile uint8_t *destination =
        (volatile uint8_t *)BIOS_DISK_BUFFER_ADDRESS;
    for (uint32_t index = 0u; index < BIOS_DISK_SECTOR_SIZE; ++index) {
        destination[index] = buffer[index];
    }
    return transfer_sector(lba, BIOS_DISK_OPERATION_WRITE);
}
