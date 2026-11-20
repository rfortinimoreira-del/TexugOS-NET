#include <stdint.h>

#include "ahci.h"
#include "paging.h"
#include "pci.h"

#define AHCI_CLASS 0x01u
#define AHCI_SUBCLASS 0x06u
#define AHCI_INTERFACE 0x01u
#define AHCI_BAR_SIZE 0x1100u
#define AHCI_PORT_BASE 0x100u
#define AHCI_PORT_STRIDE 0x80u
#define AHCI_TIMEOUT 100000u
#define AHCI_GHC_AE 0x80000000u
#define AHCI_GHC_HR 0x00000001u
#define AHCI_CAP2_BOH 0x00000001u
#define AHCI_BOHC_BOS 0x00000001u
#define AHCI_BOHC_OOS 0x00000002u
#define AHCI_PORT_CMD_ST 0x00000001u
#define AHCI_PORT_CMD_FRE 0x00000010u
#define AHCI_PORT_CMD_FR 0x00004000u
#define AHCI_PORT_CMD_CR 0x00008000u
#define AHCI_PORT_TFD_BSY 0x00000080u
#define AHCI_PORT_TFD_DRQ 0x00000008u
#define AHCI_PORT_IS_TFES 0x40000000u
#define AHCI_PORT_SIG_SATA 0x00000101u
#define AHCI_PORT_SSTS_DET_MASK 0x0fu
#define AHCI_PORT_SSTS_IPM_MASK 0x0f00u
#define AHCI_PORT_SSTS_IPM_ACTIVE 0x0100u
#define AHCI_SECTOR_SIZE 512u
#define AHCI_IDENTIFY 0xecu
#define AHCI_READ_DMA_EXT 0x25u
#define AHCI_WRITE_DMA_EXT 0x35u
#define AHCI_FIS_REG_H2D 0x27u
#define AHCI_FIS_COMMAND 0x80u
#define AHCI_DEVICE_LBA 0x40u
#define AHCI_PRDT_BYTES (AHCI_SECTOR_SIZE - 1u)
#define AHCI_PRDT_INTERRUPT 0x80000000u

struct ahci_command_header {
    uint16_t flags;
    uint16_t prdt_length;
    uint32_t transferred_bytes;
    uint32_t command_table;
    uint32_t command_table_upper;
    uint32_t reserved[4];
} __attribute__((packed));

struct ahci_prdt_entry {
    uint32_t data_base;
    uint32_t data_base_upper;
    uint32_t reserved;
    uint32_t byte_count;
} __attribute__((packed));

struct ahci_command_table {
    uint8_t command_fis[64];
    uint8_t atapi_command[16];
    uint8_t reserved[48];
    struct ahci_prdt_entry prdt[1];
} __attribute__((packed, aligned(128)));

struct ahci_port_memory {
    struct ahci_command_header command_list[32];
    uint8_t received_fis[256];
    struct ahci_command_table command_table;
} __attribute__((packed, aligned(1024)));

_Static_assert(sizeof(struct ahci_command_header) == 32u,
               "AHCI command headers must be 32 bytes");
_Static_assert(__builtin_offsetof(struct ahci_port_memory, received_fis) ==
                   1024u,
               "AHCI received-FIS area must follow the 1 KiB command list");
_Static_assert(__builtin_offsetof(struct ahci_port_memory, command_table) %
                   128u == 0u,
               "AHCI command table must be 128-byte aligned");

static struct ahci_port_memory port_memory __attribute__((aligned(1024)));
static uint8_t dma_buffer[AHCI_SECTOR_SIZE] __attribute__((aligned(2)));
static volatile uint32_t *controller_registers;
static volatile uint32_t *port_registers;
static uint32_t disk_sectors;
static char disk_name[41];
static int disk_ready;

static uint32_t read_register(volatile uint32_t *registers, uint32_t offset)
{
    return registers[offset / sizeof(uint32_t)];
}

static void write_register(volatile uint32_t *registers, uint32_t offset,
                           uint32_t value)
{
    registers[offset / sizeof(uint32_t)] = value;
}

static int wait_register_clear(volatile uint32_t *registers, uint32_t offset,
                               uint32_t mask)
{
    for (uint32_t attempt = 0u; attempt < AHCI_TIMEOUT; ++attempt) {
        if ((read_register(registers, offset) & mask) == 0u) {
            return 1;
        }
    }
    return 0;
}

static int stop_port(void)
{
    uint32_t command = read_register(port_registers, 0x18u);
    write_register(port_registers, 0x18u, command & ~AHCI_PORT_CMD_ST);
    if (!wait_register_clear(port_registers, 0x18u, AHCI_PORT_CMD_CR)) {
        return 0;
    }
    command = read_register(port_registers, 0x18u);
    write_register(port_registers, 0x18u, command & ~AHCI_PORT_CMD_FRE);
    return wait_register_clear(port_registers, 0x18u, AHCI_PORT_CMD_FR);
}

static int start_port(void)
{
    uint32_t command = read_register(port_registers, 0x18u);
    write_register(port_registers, 0x18u, command | AHCI_PORT_CMD_FRE);
    command = read_register(port_registers, 0x18u);
    write_register(port_registers, 0x18u, command | AHCI_PORT_CMD_ST);
    for (uint32_t attempt = 0u; attempt < AHCI_TIMEOUT; ++attempt) {
        if ((read_register(port_registers, 0x20u) &
             (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) == 0u) {
            return 1;
        }
    }
    return 0;
}

static void clear_memory(void *memory, uint32_t size)
{
    uint8_t *bytes = (uint8_t *)memory;
    for (uint32_t index = 0u; index < size; ++index) {
        bytes[index] = 0u;
    }
}

static int issue_command(uint8_t command, uint32_t lba, int write,
                         int use_lba)
{
    struct ahci_command_header *header = &port_memory.command_list[0];
    struct ahci_command_table *table = &port_memory.command_table;
    clear_memory(header, sizeof(*header));
    clear_memory(table, sizeof(*table));
    header->flags = (uint16_t)(5u | (write ? (1u << 6) : 0u));
    header->prdt_length = 1u;
    header->command_table = (uint32_t)(uintptr_t)table;

    uint8_t *fis = table->command_fis;
    fis[0] = AHCI_FIS_REG_H2D;
    fis[1] = AHCI_FIS_COMMAND;
    fis[2] = command;
    if (use_lba) {
        fis[4] = (uint8_t)lba;
        fis[5] = (uint8_t)(lba >> 8);
        fis[6] = (uint8_t)(lba >> 16);
        fis[7] = AHCI_DEVICE_LBA;
        fis[8] = (uint8_t)(lba >> 24);
        fis[12] = 1u;
    }

    struct ahci_prdt_entry *prdt = &table->prdt[0];
    prdt->data_base = (uint32_t)(uintptr_t)dma_buffer;
    prdt->byte_count = AHCI_PRDT_INTERRUPT | AHCI_PRDT_BYTES;
    write_register(port_registers, 0x10u, 0xffffffffu);
    write_register(port_registers, 0x30u, 0xffffffffu);
    __asm__ volatile ("" : : : "memory");
    write_register(port_registers, 0x38u, 1u);

    for (uint32_t attempt = 0u; attempt < AHCI_TIMEOUT; ++attempt) {
        uint32_t interrupt_status = read_register(port_registers, 0x10u);
        if ((interrupt_status & AHCI_PORT_IS_TFES) != 0u) {
            return 0;
        }
        if ((read_register(port_registers, 0x38u) & 1u) == 0u) {
            return 1;
        }
    }
    return 0;
}

static void copy_identify_model(const uint16_t *identify)
{
    for (uint32_t index = 0u; index < 20u; ++index) {
        disk_name[index * 2u] = (char)(identify[27u + index] >> 8);
        disk_name[index * 2u + 1u] = (char)identify[27u + index];
    }
    disk_name[40] = '\0';
    for (int32_t end = 39; end >= 0 && disk_name[end] == ' '; --end) {
        disk_name[end] = '\0';
    }
}

static int initialize_controller(void)
{
    struct pci_device device;
    int found = 0;
    for (uint32_t index = 0u; index < pci_device_count(); ++index) {
        if (pci_get_device(index, &device) &&
            device.class_code == AHCI_CLASS &&
            device.subclass == AHCI_SUBCLASS &&
            device.programming_interface == AHCI_INTERFACE) {
            found = 1;
            break;
        }
    }
    if (!found) {
        return 0;
    }

    struct pci_bar bar;
    if (!pci_read_bar(&device, 5u, &bar) || bar.is_io != 0u ||
        bar.address > UINT32_MAX) {
        return 0;
    }
    if (!pci_enable_memory_bus_mastering(&device)) {
        return 0;
    }

    uint32_t base = (uint32_t)bar.address;
    if (base >= 16u * 1024u * 1024u &&
        !paging_map_mmio(base, AHCI_BAR_SIZE)) {
        return 0;
    }
    controller_registers = (volatile uint32_t *)(uintptr_t)base;
    uint32_t capability2 = read_register(controller_registers, 0x24u);
    if ((capability2 & AHCI_CAP2_BOH) != 0u) {
        uint32_t handoff = read_register(controller_registers, 0x28u);
        write_register(controller_registers, 0x28u,
                       handoff | AHCI_BOHC_OOS);
        for (uint32_t attempt = 0u; attempt < AHCI_TIMEOUT; ++attempt) {
            if ((read_register(controller_registers, 0x28u) &
                 AHCI_BOHC_BOS) == 0u) {
                break;
            }
            if (attempt + 1u == AHCI_TIMEOUT) {
                return 0;
            }
        }
    }
    uint32_t global_host = read_register(controller_registers, 0x04u);
    write_register(controller_registers, 0x04u, global_host | AHCI_GHC_AE);
    if ((read_register(controller_registers, 0x04u) & AHCI_GHC_AE) == 0u) {
        return 0;
    }
    return 1;
}

int ahci_initialize(void)
{
    disk_ready = 0;
    disk_sectors = 0u;
    if (!initialize_controller()) {
        return 0;
    }

    uint32_t implemented = read_register(controller_registers, 0x0cu);
    for (uint32_t index = 0u; index < 32u; ++index) {
        if ((implemented & (1u << index)) == 0u) {
            continue;
        }
        volatile uint32_t *candidate =
            (volatile uint32_t *)((uintptr_t)controller_registers +
                                  AHCI_PORT_BASE + index * AHCI_PORT_STRIDE);
        uint32_t sata_status = read_register(candidate, 0x28u);
        if ((sata_status & AHCI_PORT_SSTS_DET_MASK) != 3u ||
            (sata_status & AHCI_PORT_SSTS_IPM_MASK) !=
                AHCI_PORT_SSTS_IPM_ACTIVE ||
            read_register(candidate, 0x24u) != AHCI_PORT_SIG_SATA) {
            continue;
        }

        port_registers = candidate;
        if (!stop_port()) {
            continue;
        }
        clear_memory(&port_memory, sizeof(port_memory));
        write_register(port_registers, 0x00u,
                       (uint32_t)(uintptr_t)port_memory.command_list);
        write_register(port_registers, 0x04u, 0u);
        write_register(port_registers, 0x08u,
                       (uint32_t)(uintptr_t)port_memory.received_fis);
        write_register(port_registers, 0x0cu, 0u);
        write_register(port_registers, 0x10u, 0xffffffffu);
        write_register(port_registers, 0x30u, 0xffffffffu);
        if (!start_port()) {
            continue;
        }

        clear_memory(dma_buffer, sizeof(dma_buffer));
        if (!issue_command(AHCI_IDENTIFY, 0u, 0, 0)) {
            continue;
        }
        const uint16_t *identify = (const uint16_t *)dma_buffer;
        uint64_t sectors = (uint64_t)identify[100] |
                           ((uint64_t)identify[101] << 16) |
                           ((uint64_t)identify[102] << 32) |
                           ((uint64_t)identify[103] << 48);
        if ((identify[83] & (1u << 10)) == 0u || sectors == 0u) {
            sectors = (uint64_t)identify[60] |
                      ((uint64_t)identify[61] << 16);
        }
        if (sectors == 0u) {
            continue;
        }
        disk_sectors = sectors > UINT32_MAX ? UINT32_MAX : (uint32_t)sectors;
        copy_identify_model(identify);
        disk_ready = 1;
        return 1;
    }
    return 0;
}

const char *ahci_model(void)
{
    return disk_name;
}

uint32_t ahci_sector_count(void)
{
    return disk_ready ? disk_sectors : 0u;
}

int ahci_read_sector(uint32_t lba, uint8_t *buffer)
{
    if (!disk_ready || buffer == 0 || lba >= disk_sectors) {
        return 0;
    }
    if (!issue_command(AHCI_READ_DMA_EXT, lba, 0, 1)) {
        return 0;
    }
    for (uint32_t index = 0u; index < AHCI_SECTOR_SIZE; ++index) {
        buffer[index] = dma_buffer[index];
    }
    return 1;
}

int ahci_write_sector(uint32_t lba, const uint8_t *buffer)
{
    if (!disk_ready || buffer == 0 || lba >= disk_sectors) {
        return 0;
    }
    for (uint32_t index = 0u; index < AHCI_SECTOR_SIZE; ++index) {
        dma_buffer[index] = buffer[index];
    }
    return issue_command(AHCI_WRITE_DMA_EXT, lba, 1, 1);
}
