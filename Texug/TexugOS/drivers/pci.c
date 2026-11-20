#include <stdint.h>

#include "pci.h"

#define PCI_CONFIG_ADDRESS 0x0cf8u
#define PCI_CONFIG_DATA 0x0cfcu
#define PCI_MAX_DEVICES 256u
#define PCI_FUNCTIONS_PER_SLOT 8u

static struct pci_device devices[PCI_MAX_DEVICES];
static uint32_t device_count;
static int inventory_truncated;

static void out32(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static uint32_t in32(uint16_t port)
{
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t read_config(uint8_t bus, uint8_t slot, uint8_t function,
                            uint8_t offset)
{
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) | (offset & 0xfcu);
    out32(PCI_CONFIG_ADDRESS, address);
    return in32(PCI_CONFIG_DATA);
}

static void enqueue_bus(uint8_t buses[256], uint8_t visited[256],
                        uint32_t *bus_count, uint8_t bus)
{
    if (visited[bus] != 0u) {
        return;
    }
    visited[bus] = 1u;
    buses[(*bus_count)++] = bus;
}

static void out16(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t function,
                           uint8_t offset)
{
    return read_config(bus, slot, function, offset);
}

int pci_read_bar(const struct pci_device *device, uint8_t bar_index,
                 struct pci_bar *bar)
{
    if (device == 0 || bar == 0 || bar_index >= 6u) {
        return 0;
    }

    uint8_t offset = (uint8_t)(0x10u + bar_index * 4u);
    uint32_t value = pci_read_config32(device->bus, device->slot,
                                       device->function, offset);
    bar->is_io = (uint8_t)(value & 1u);
    bar->is_64bit = 0u;
    bar->address = 0u;
    if (bar->is_io != 0u) {
        bar->address = value & 0xfffffffcu;
        return bar->address != 0u;
    }

    uint32_t memory_type = (value >> 1) & 3u;
    if (memory_type == 3u) {
        return 0;
    }
    bar->address = value & 0xfffffff0u;
    if (memory_type == 2u) {
        if (bar_index >= 5u) {
            return 0;
        }
        uint32_t high = pci_read_config32(device->bus, device->slot,
                                          device->function,
                                          (uint8_t)(offset + 4u));
        bar->is_64bit = 1u;
        bar->address |= (uint64_t)high << 32;
    }
    return bar->address != 0u;
}

void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t function,
                        uint8_t offset, uint16_t value)
{
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) | (offset & 0xfcu);
    out32(PCI_CONFIG_ADDRESS, address);
    out16((uint16_t)(PCI_CONFIG_DATA + (offset & 2u)), value);
}

int pci_enable_memory_bus_mastering(const struct pci_device *device)
{
    if (!pci_enable_memory_space(device)) {
        return 0;
    }

    return pci_enable_bus_mastering(device);
}

int pci_enable_memory_space(const struct pci_device *device)
{
    if (device == 0) {
        return 0;
    }

    uint32_t command = pci_read_config32(device->bus, device->slot,
                                         device->function, 0x04u);
    pci_write_config16(device->bus, device->slot, device->function, 0x04u,
                       (uint16_t)(command | 0x0002u));
    command = pci_read_config32(device->bus, device->slot,
                                device->function, 0x04u);
    return (command & 0x0002u) != 0u;
}

int pci_enable_bus_mastering(const struct pci_device *device)
{
    if (device == 0) {
        return 0;
    }
    uint32_t command = pci_read_config32(device->bus, device->slot,
                                         device->function, 0x04u);
    if ((command & 0x0002u) == 0u) {
        return 0;
    }
    pci_write_config16(device->bus, device->slot, device->function, 0x04u,
                       (uint16_t)(command | 0x0004u));
    command = pci_read_config32(device->bus, device->slot,
                                device->function, 0x04u);
    return (command & 0x0006u) == 0x0006u;
}

void pci_initialize(void)
{
    device_count = 0u;
    inventory_truncated = 0;
    uint8_t buses[256] = {0};
    uint8_t visited[256] = {0};
    uint32_t bus_count = 0u;
    enqueue_bus(buses, visited, &bus_count, 0u);

    for (uint32_t bus_index = 0u; bus_index < bus_count; ++bus_index) {
        uint8_t bus = buses[bus_index];
        for (uint32_t slot = 0; slot < 32u; ++slot) {
            uint32_t identity = read_config(bus, (uint8_t)slot, 0u, 0u);
            if ((identity & 0xffffu) == 0xffffu) {
                continue;
            }

            uint32_t header =
                read_config(bus, (uint8_t)slot, 0u, 0x0cu);
            uint32_t function_count =
                (header & 0x00800000u) != 0u ? PCI_FUNCTIONS_PER_SLOT : 1u;
            for (uint32_t function = 0; function < function_count; ++function) {
                identity = read_config(bus, (uint8_t)slot,
                                       (uint8_t)function, 0u);
                uint16_t vendor = (uint16_t)identity;
                if (vendor == 0xffffu) {
                    continue;
                }
                uint32_t function_header =
                    read_config(bus, (uint8_t)slot, (uint8_t)function,
                                0x0cu);
                uint32_t class_info =
                    read_config(bus, (uint8_t)slot, (uint8_t)function,
                                0x08u);
                if (device_count == PCI_MAX_DEVICES) {
                    inventory_truncated = 1;
                } else {
                    devices[device_count++] = (struct pci_device) {
                        .bus = bus,
                        .slot = (uint8_t)slot,
                        .function = (uint8_t)function,
                        .class_code = (uint8_t)(class_info >> 24),
                        .subclass = (uint8_t)(class_info >> 16),
                        .programming_interface =
                            (uint8_t)(class_info >> 8),
                        .vendor_id = vendor,
                        .device_id = (uint16_t)(identity >> 16),
                    };
                }

                uint8_t class_code = (uint8_t)(class_info >> 24);
                uint8_t subclass = (uint8_t)(class_info >> 16);
                uint8_t header_type =
                    (uint8_t)((function_header >> 16) & 0x7fu);
                if (class_code == 0x06u && subclass == 0x04u &&
                    header_type == 0x01u) {
                    uint32_t bus_numbers =
                        read_config(bus, (uint8_t)slot,
                                    (uint8_t)function, 0x18u);
                    uint8_t primary = (uint8_t)bus_numbers;
                    uint8_t secondary = (uint8_t)(bus_numbers >> 8);
                    if (primary == bus && secondary != 0u) {
                        enqueue_bus(buses, visited, &bus_count, secondary);
                    }
                }
            }
        }
    }
}

uint32_t pci_device_count(void)
{
    return device_count <= PCI_MAX_DEVICES ? device_count : PCI_MAX_DEVICES;
}

int pci_inventory_truncated(void)
{
    return inventory_truncated;
}

int pci_get_device(uint32_t index, struct pci_device *device)
{
    if (device == 0 || index >= PCI_MAX_DEVICES ||
        index >= pci_device_count()) {
        return 0;
    }
    *device = devices[index];
    return 1;
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id,
                    struct pci_device *device)
{
    if (device == 0) {
        return 0;
    }
    for (uint32_t index = 0u; index < pci_device_count(); ++index) {
        if (devices[index].vendor_id == vendor_id &&
            devices[index].device_id == device_id) {
            *device = devices[index];
            return 1;
        }
    }
    return 0;
}
