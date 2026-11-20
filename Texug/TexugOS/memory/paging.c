#include <stdint.h>

#include "paging.h"

#define PAGE_SIZE 4096u
#define PAGE_ENTRIES 1024u
#define IDENTITY_TABLES 4u
#define MMIO_TABLES 8u
#define MMIO_TABLE_BASE IDENTITY_TABLES
#define TOTAL_TABLES (IDENTITY_TABLES + MMIO_TABLES)
#define INVALID_DIRECTORY UINT32_MAX
#define PAGE_PRESENT_WRITE 0x003u
#define PAGE_WRITE_THROUGH 0x008u
#define PAGE_CACHE_DISABLE 0x010u
#define PAGE_USER 0x004u
#define IDENTITY_MAP_SIZE (IDENTITY_TABLES * PAGE_ENTRIES * PAGE_SIZE)

static uint32_t page_directory[PAGE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint32_t page_tables[TOTAL_TABLES][PAGE_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));
static uint32_t mmio_directories[MMIO_TABLES];

void paging_initialize(void)
{
    for (uint32_t index = 0; index < PAGE_ENTRIES; ++index) {
        page_directory[index] = 0;
    }

    for (uint32_t table = 0; table < IDENTITY_TABLES; ++table) {
        page_directory[table] =
            (uint32_t)(uintptr_t)page_tables[table] | PAGE_PRESENT_WRITE;
        for (uint32_t page = 0; page < PAGE_ENTRIES; ++page) {
            uint32_t physical_address =
                (table * PAGE_ENTRIES + page) * PAGE_SIZE;
            page_tables[table][page] = physical_address | PAGE_PRESENT_WRITE;
        }
    }
    for (uint32_t table = MMIO_TABLE_BASE; table < TOTAL_TABLES; ++table) {
        mmio_directories[table - MMIO_TABLE_BASE] = INVALID_DIRECTORY;
        for (uint32_t page = 0u; page < PAGE_ENTRIES; ++page) {
            page_tables[table][page] = 0u;
        }
    }

    uint32_t directory_address = (uint32_t)(uintptr_t)page_directory;
    uint32_t cr0;
    __asm__ volatile ("movl %0, %%cr3" : : "r"(directory_address) : "memory");
    __asm__ volatile ("movl %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile ("movl %0, %%cr0" : : "r"(cr0) : "memory");
}

int paging_map_mmio(uint32_t address, uint32_t size)
{
    if (size == 0u || address > UINT32_MAX - size) {
        return 0;
    }
    uint32_t first = address & ~(PAGE_SIZE - 1u);
    uint32_t end = address + size;
    if (end > UINT32_MAX - (PAGE_SIZE - 1u)) {
        return 0;
    }
    end = (end + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
    uint32_t first_directory = first >> 22;
    uint32_t last_directory = (end - 1u) >> 22;
    uint32_t directory_count = last_directory - first_directory + 1u;
    if (last_directory >= PAGE_ENTRIES ||
        directory_count > IDENTITY_TABLES + MMIO_TABLES) {
        return 0;
    }

    uint32_t selected_tables[IDENTITY_TABLES + MMIO_TABLES];
    for (uint32_t offset = 0u; offset < directory_count; ++offset) {
        uint32_t directory = first_directory + offset;
        if (directory < IDENTITY_TABLES) {
            selected_tables[offset] = directory;
            continue;
        }
        uint32_t slot = MMIO_TABLES;
        for (uint32_t index = 0u; index < MMIO_TABLES; ++index) {
            if (mmio_directories[index] == directory) {
                slot = index;
                break;
            }
        }
        if (slot == MMIO_TABLES) {
            for (uint32_t index = 0u; index < MMIO_TABLES; ++index) {
                if (mmio_directories[index] == INVALID_DIRECTORY) {
                    int already_selected = 0;
                    for (uint32_t selected = 0u; selected < offset;
                         ++selected) {
                        if (selected_tables[selected] ==
                            MMIO_TABLE_BASE + index) {
                            already_selected = 1;
                            break;
                        }
                    }
                    if (!already_selected) {
                        slot = index;
                        break;
                    }
                }
            }
        }
        if (slot == MMIO_TABLES) {
            return 0;
        }
        selected_tables[offset] = MMIO_TABLE_BASE + slot;
    }

    for (uint32_t offset = 0u; offset < directory_count; ++offset) {
        uint32_t directory = first_directory + offset;
        uint32_t table_index = selected_tables[offset];
        if (table_index >= MMIO_TABLE_BASE &&
            mmio_directories[table_index - MMIO_TABLE_BASE] ==
                INVALID_DIRECTORY) {
            mmio_directories[table_index - MMIO_TABLE_BASE] = directory;
            page_directory[directory] =
                (uint32_t)(uintptr_t)page_tables[table_index] |
                PAGE_PRESENT_WRITE;
        }
    }

    for (uint32_t page_address = first; page_address < end;
         page_address += PAGE_SIZE) {
        uint32_t directory = page_address >> 22;
        uint32_t offset = directory - first_directory;
        uint32_t index = (page_address >> 12) & (PAGE_ENTRIES - 1u);
        page_tables[selected_tables[offset]][index] =
            page_address | PAGE_PRESENT_WRITE | PAGE_WRITE_THROUGH |
            PAGE_CACHE_DISABLE;
    }
    uint32_t directory_address = (uint32_t)(uintptr_t)page_directory;
    __asm__ volatile ("movl %0, %%cr3" : : "r"(directory_address) : "memory");
    return 1;
}

int paging_set_user_page(uint32_t address, int user, int writable)
{
    if (address >= IDENTITY_MAP_SIZE ||
        (address & (PAGE_SIZE - 1u)) != 0u) {
        return 0;
    }

    uint32_t directory_index = address >> 22;
    uint32_t table_index = (address >> 12) & (PAGE_ENTRIES - 1u);
    uint32_t *entry = &page_tables[directory_index][table_index];
    if (user) {
        page_directory[directory_index] |= PAGE_USER;
        *entry |= PAGE_USER;
        if (writable) {
            *entry |= 0x002u;
        } else {
            *entry &= ~0x002u;
        }
    } else {
        *entry = (address & ~(PAGE_SIZE - 1u)) | PAGE_PRESENT_WRITE;
        if (page_directory[directory_index] & PAGE_USER) {
            int has_user_page = 0;
            for (uint32_t index = 0u; index < PAGE_ENTRIES; ++index) {
                if ((page_tables[directory_index][index] & PAGE_USER) != 0u) {
                    has_user_page = 1;
                    break;
                }
            }
            if (!has_user_page) {
                page_directory[directory_index] &= ~PAGE_USER;
            }
        }
    }

    uint32_t directory_address = (uint32_t)(uintptr_t)page_directory;
    __asm__ volatile ("movl %0, %%cr3" : : "r"(directory_address) : "memory");
    return 1;
}

uint32_t paging_fault_address(void)
{
    uint32_t address;
    __asm__ volatile ("movl %%cr2, %0" : "=r"(address));
    return address;
}
