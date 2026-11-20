#include <stdint.h>

#include "console.h"
#include "interrupts.h"
#include "frame_allocator.h"
#include "heap.h"
#include "paging.h"
#include "pic.h"
#include "pit.h"
#include "threads.h"
#include "ata.h"
#include "fat32.h"
#include "keyboard.h"
#include "shell.h"
#include "mouse.h"
#include "pci.h"
#include "network.h"
#include "rtl8192se.h"
#include "rtl8192se_firmware.h"
#include "user_mode.h"

#define E820_BUFFER ((volatile const struct e820_entry *)0x5000u)
#define E820_MAX_ENTRIES 32u
#define BOOT_MAGIC 0x54455847u

static volatile uint32_t thread_a_runs;
static volatile uint32_t thread_b_runs;

static const char *rtl8192se_firmware_failure_name(
    enum rtl8192se_firmware_upload_failure failure)
{
    switch (failure) {
    case RTL8192SE_FW_UPLOAD_IMEM_TRANSFER:
        return "IMEM DMA";
    case RTL8192SE_FW_UPLOAD_IMEM_CHECK:
        return "IMEM verification";
    case RTL8192SE_FW_UPLOAD_EMEM_TRANSFER:
        return "EMEM DMA";
    case RTL8192SE_FW_UPLOAD_EMEM_CHECK:
        return "EMEM verification";
    case RTL8192SE_FW_UPLOAD_CPU_READY:
        return "CPU/IMEM ready";
    case RTL8192SE_FW_UPLOAD_DMEM_TRANSFER:
        return "private-data DMA";
    case RTL8192SE_FW_UPLOAD_DMEM_CHECK:
        return "private-data verification";
    case RTL8192SE_FW_UPLOAD_FIRMWARE_READY:
        return "firmware-ready wait";
    case RTL8192SE_FW_UPLOAD_INVALID_INPUT:
        return "invalid upload configuration";
    case RTL8192SE_FW_UPLOAD_OK:
    default:
        return "unknown upload stage";
    }
}

static void report_rtl8192se_status(void)
{
    struct pci_device adapter;
    if (!pci_find_device(0x10ecu, 0x8171u, &adapter)) {
        return;
    }

    struct rtl8192se_status status;
    if (!rtl8192se_probe(&status)) {
        console_write("Wi-Fi: RTL8191SE found, but its PCI MMIO BAR "
                      "could not be mapped\n");
        return;
    }

    console_write("Wi-Fi: RTL8191SE PCI/MMIO probe passed; "
                  "firmware is not running\n");
    struct rtl8192se_firmware firmware;
    if (!rtl8192se_firmware_load(&firmware)) {
        console_write("Wi-Fi: RTL8192.BIN missing or invalid; "
                      "connection unavailable\n");
        return;
    }

    console_write("Wi-Fi: firmware parsed, version ");
    console_write_u32(firmware.version);
    console_write("; checking adapter eFuse and uploading\n");
    if (!rtl8192se_start_firmware(&status, &firmware)) {
        if (status.dma_prepared == 0u) {
            console_write("Wi-Fi: command DMA memory preparation failed\n");
        } else if (status.rx_dma_prepared == 0u) {
            console_write("Wi-Fi: RX DMA ring/buffer allocation failed\n");
        } else if (status.tx_dma_prepared == 0u) {
            console_write("Wi-Fi: TX DMA ring/buffer allocation failed\n");
        } else if (status.mac_power_ready == 0u) {
            console_write("Wi-Fi: RTL8191SE MAC power/clock setup failed\n");
        } else if (status.adapter_config_read == 0u) {
            console_write("Wi-Fi: eFuse data is unreadable or has an "
                          "unsupported MAC/board configuration\n");
        } else if (status.firmware_uploaded != 0u) {
            if (status.phy_initialized == 0u) {
                console_write("Wi-Fi: firmware loaded, but PHY/RF "
                              "initialization failed\n");
            } else if (status.rx_rings_programmed == 0u) {
                console_write("Wi-Fi: firmware loaded, but RX ring base "
                              "programming failed\n");
            } else if (status.tx_rings_programmed == 0u) {
                console_write("Wi-Fi: firmware loaded, but TX ring base "
                              "programming failed\n");
            } else {
                console_write("Wi-Fi: firmware loaded, but MAC/RX/TX "
                              "engine activation failed\n");
            }
        } else {
            console_write("Wi-Fi: RTL8191SE ");
            console_write(rtl8192se_firmware_failure_name(
                status.firmware_upload_failure));
            console_write(" failed, TCR=");
            console_write_hex32(status.firmware_upload_tcr);
            console_write("; connection unavailable\n");
        }
        return;
    }
    console_write("Wi-Fi: firmware, PHY/RF, and MAC/RX/TX DMA initialized; "
                  "association incomplete\n");
}

static int create_filesystem_test_directory(uint32_t root, char name[9],
                                            uint32_t *cluster)
{
    char candidate[9] = "TST00000";
    for (uint32_t attempt = 0u; attempt < 100000u; ++attempt) {
        uint32_t value = attempt;
        for (int32_t index = 7; index >= 3; --index) {
            candidate[index] = (char)('0' + value % 10u);
            value /= 10u;
        }

        struct fat32_entry existing;
        if (fat32_find(root, candidate, &existing)) {
            continue;
        }
        if (!fat32_create_directory(root, candidate, cluster)) {
            return 0;
        }
        for (uint32_t index = 0u; index < sizeof(candidate); ++index) {
            name[index] = candidate[index];
        }
        return 1;
    }
    return 0;
}

static int filesystem_self_test(void)
{
    uint32_t root = fat32_root_cluster();
    uint32_t test_root;
    char test_directory_name[9];
    uint32_t directory;
    uint8_t expected[700];
    uint8_t actual[700];
    for (uint32_t index = 0; index < sizeof(expected); ++index) {
        expected[index] = (uint8_t)('A' + index % 26u);
    }

    if (root == 0u ||
        !create_filesystem_test_directory(root, test_directory_name,
                                          &test_root) ||
        !fat32_create_directory(test_root, "KERNEL", &directory)) {
        console_write("FAT32 mkdir self-test failed.\n");
        return 0;
    }
    if (!fat32_write_file(directory, "BOOT.BIN", expected, sizeof(expected))) {
        console_write("FAT32 file write self-test failed.\n");
        return 0;
    }

    uint32_t size = 0;
    if (!fat32_read_file(directory, "BOOT.BIN", actual, sizeof(actual), &size) ||
        size != sizeof(expected)) {
        console_write("FAT32 file read self-test failed.\n");
        return 0;
    }
    for (uint32_t index = 0; index < size; ++index) {
        if (actual[index] != expected[index]) {
            console_write("FAT32 data comparison failed.\n");
            return 0;
        }
    }
    if (!fat32_delete(directory, "BOOT.BIN")) {
        console_write("FAT32 file delete self-test failed.\n");
        return 0;
    }
    if (!fat32_delete(test_root, "KERNEL")) {
        console_write("FAT32 directory delete self-test failed.\n");
        return 0;
    }

    static const char long_file_name[] =
        "Long filename across VFAT entries.txt";
    if (!fat32_write_file(test_root, long_file_name, expected,
                          sizeof(expected))) {
        console_write("FAT32 long filename write self-test failed.\n");
        return 0;
    }
    struct fat32_entry long_file;
    if (!fat32_find(test_root, "LONG FILENAME ACROSS VFAT ENTRIES.TXT",
                    &long_file) ||
        !fat32_find(test_root, "LONGFI~1.TXT", &long_file) ||
        !fat32_read_file(test_root, long_file_name, actual, sizeof(actual),
                         &size) ||
        size != sizeof(expected)) {
        console_write("FAT32 long filename lookup self-test failed.\n");
        return 0;
    }
    for (uint32_t index = 0u; index < size; ++index) {
        if (actual[index] != expected[index]) {
            console_write("FAT32 long filename data test failed.\n");
            return 0;
        }
    }
    struct fat32_entry listed[4];
    uint32_t listed_count;
    if (!fat32_list(test_root, listed, 4u, &listed_count) ||
        listed_count != 3u) {
        console_write("FAT32 long filename list self-test failed.\n");
        return 0;
    }
    uint32_t listed_file = listed_count;
    for (uint32_t index = 0u; index < listed_count; ++index) {
        if (listed[index].name[0] != '.') {
            listed_file = index;
            break;
        }
    }
    if (listed_file == listed_count) {
        console_write("FAT32 long filename list self-test failed.\n");
        return 0;
    }
    for (uint32_t index = 0u; index < sizeof(long_file_name); ++index) {
        if (listed[listed_file].name[index] != long_file_name[index]) {
            console_write("FAT32 long filename display test failed.\n");
            return 0;
        }
    }
    struct fat32_entry long_directory;
    uint32_t long_directory_cluster;
    static const char long_directory_name[] =
        "Long directory with spaces";
    if (!fat32_create_directory(test_root, long_directory_name,
                                &long_directory_cluster) ||
        !fat32_find(test_root, "long directory with spaces",
                    &long_directory) ||
        (long_directory.attributes & FAT32_ATTRIBUTE_DIRECTORY) == 0u ||
        long_directory.first_cluster != long_directory_cluster ||
        !fat32_delete(test_root, long_directory_name) ||
        !fat32_delete(test_root, long_file_name) ||
        fat32_find(test_root, "LONGFI~1.TXT", &long_file)) {
        console_write("FAT32 long filename CRUD self-test failed.\n");
        return 0;
    }

    char maximum_name[256];
    for (uint32_t index = 0u; index < sizeof(maximum_name) - 1u; ++index) {
        maximum_name[index] = 'a';
    }
    maximum_name[sizeof(maximum_name) - 1u] = '\0';
    if (!fat32_write_file(test_root, maximum_name, 0, 0u) ||
        !fat32_find(test_root, maximum_name, &long_file) ||
        !fat32_find(test_root, "AAAAAA~1", &long_file) ||
        !fat32_list(test_root, listed, 4u, &listed_count) ||
        listed_count != 3u) {
        console_write("FAT32 maximum-length filename test failed.\n");
        return 0;
    }
    listed_file = listed_count;
    for (uint32_t index = 0u; index < listed_count; ++index) {
        if (listed[index].name[0] != '.') {
            listed_file = index;
            break;
        }
    }
    if (listed_file == listed_count) {
        console_write("FAT32 maximum-length filename display failed.\n");
        return 0;
    }
    for (uint32_t index = 0u; index < sizeof(maximum_name); ++index) {
        if (listed[listed_file].name[index] != maximum_name[index]) {
            console_write("FAT32 maximum-length filename display failed.\n");
            return 0;
        }
    }
    if (!fat32_delete(test_root, maximum_name) ||
        fat32_find(test_root, "AAAAAA~1", &long_file)) {
        console_write("FAT32 maximum-length filename delete failed.\n");
        return 0;
    }

    uint32_t growth_directory;
    char filler_name[9] = "FILL0000";
    if (!fat32_create_directory(test_root, "GROWTH", &growth_directory)) {
        console_write("FAT32 directory growth setup failed.\n");
        return 0;
    }
    for (uint32_t index = 0u; index < 13u; ++index) {
        filler_name[6] = (char)('0' + index / 10u);
        filler_name[7] = (char)('0' + index % 10u);
        if (!fat32_write_file(growth_directory, filler_name, 0, 0u)) {
            console_write("FAT32 directory growth fill failed.\n");
            return 0;
        }
    }
    if (!fat32_write_file(growth_directory, maximum_name, 0, 0u) ||
        !fat32_find(growth_directory, maximum_name, &long_file) ||
        long_file.size != 0u ||
        !fat32_delete(growth_directory, maximum_name)) {
        console_write("FAT32 multi-cluster directory growth failed.\n");
        return 0;
    }
    for (uint32_t index = 0u; index < 13u; ++index) {
        filler_name[6] = (char)('0' + index / 10u);
        filler_name[7] = (char)('0' + index % 10u);
        if (!fat32_delete(growth_directory, filler_name)) {
            console_write("FAT32 directory growth cleanup failed.\n");
            return 0;
        }
    }
    if (!fat32_delete(test_root, "GROWTH") ||
        !fat32_delete(root, test_directory_name)) {
        console_write("FAT32 directory growth removal failed.\n");
        return 0;
    }
    return 1;
}

static int memory_self_test(void)
{
    uint32_t frames_before = frame_free_count();
    uint32_t first_frame = frame_allocate();
    uint32_t second_frame = frame_allocate();
    uint32_t dma_frame = frame_allocate_below(0x01000000ull);
    if (first_frame == FRAME_INVALID || second_frame == FRAME_INVALID ||
        dma_frame == FRAME_INVALID || first_frame == second_frame ||
        first_frame == dma_frame || second_frame == dma_frame ||
        dma_frame >= 0x01000000u) {
        if (first_frame != FRAME_INVALID) {
            frame_free(first_frame);
        }
        if (second_frame != FRAME_INVALID) {
            frame_free(second_frame);
        }
        if (dma_frame != FRAME_INVALID) {
            frame_free(dma_frame);
        }
        return 0;
    }

    if (first_frame < 0x01000000u) {
        volatile uint32_t *frame = (volatile uint32_t *)(uintptr_t)first_frame;
        *frame = 0x54455847u;
        if (*frame != 0x54455847u) {
            frame_free(first_frame);
            frame_free(second_frame);
            frame_free(dma_frame);
            return 0;
        }
    }
    volatile uint32_t *dma_memory =
        (volatile uint32_t *)(uintptr_t)dma_frame;
    *dma_memory = 0x444d4131u;
    if (*dma_memory != 0x444d4131u) {
        frame_free(first_frame);
        frame_free(second_frame);
        frame_free(dma_frame);
        return 0;
    }

    if (!frame_free(first_frame) || frame_free(first_frame) ||
        !frame_free(second_frame) || !frame_free(dma_frame) ||
        frame_free_count() != frames_before) {
        return 0;
    }

    void *first = kmalloc(16u);
    void *middle = kmalloc(64u);
    void *last = kmalloc(16u);
    if (first == 0 || middle == 0 || last == 0 ||
        ((uintptr_t)first & 7u) != 0u ||
        ((uintptr_t)middle & 7u) != 0u ||
        ((uintptr_t)last & 7u) != 0u) {
        return 0;
    }
    if (!kfree(middle) || kfree(middle)) {
        return 0;
    }
    void *reused = kmalloc(32u);
    if (reused == 0 || kfree((uint8_t *)first + 1u)) {
        return 0;
    }
    if (!kfree(first) || !kfree(last) || !kfree(reused)) {
        return 0;
    }

    void *large = kmalloc(32u * 1024u);
    if (large == 0 || !kfree(large)) {
        return 0;
    }
    return 1;
}

static void halt_forever(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void thread_a(void *argument)
{
    (void)argument;
    ++thread_a_runs;
    while (thread_b_runs == 0u) {
        __asm__ volatile ("pause");
    }
}

static void thread_b(void *argument)
{
    (void)argument;
    ++thread_b_runs;
    thread_yield();
}

void kernel_main(uint32_t boot_magic, uint32_t memory_map_address,
                 uint32_t memory_map_entries)
{
    __asm__ volatile ("cli" : : : "memory");
    console_clear();
    console_write("TexugOS - BIOS boot stage\n\n");
    interrupts_initialize();
    console_write("IDT: early exception handlers installed\n");

    if (boot_magic != BOOT_MAGIC || memory_map_address != 0x5000u ||
        memory_map_entries == 0u || memory_map_entries > E820_MAX_ENTRIES) {
        console_write("Boot handoff validation failed.\n");
        halt_forever();
    }

    uint64_t usable_bytes = 0;
    for (uint32_t index = 0; index < memory_map_entries; ++index) {
        if (E820_BUFFER[index].type == 1u) {
            usable_bytes += E820_BUFFER[index].length;
        }
    }

    console_write("BIOS E820 entries: ");
    console_write_u32(memory_map_entries);
    console_write("\nUsable RAM: ");
    console_write_u32((uint32_t)(usable_bytes >> 20));
    console_write(" MiB\n");
    paging_initialize();
    console_write("Paging: 16 MiB identity map active\n");

    frame_allocator_initialize(E820_BUFFER, memory_map_entries);
    heap_initialize();
    if (!memory_self_test()) {
        console_write("Memory allocator initialization failed.\n");
        halt_forever();
    }
    console_write("Memory: frame allocator and kernel heap active\n");

    console_write("PCI: scanning configured buses...\n");
    pci_initialize();
    console_write("PCI: ");
    console_write_u32(pci_device_count());
    console_write(" devices enumerated\n");
    if (pci_inventory_truncated()) {
        console_write("PCI: inventory limit reached; some devices omitted\n");
    }
    console_write("Storage: probing ATA, AHCI, and BIOS disk...\n");
    int storage_available = ata_initialize();
    if (!storage_available) {
        console_write("Storage: ATA PIO device not detected; continuing "
                      "without FAT32 storage.\n");
    } else {
        console_write("ATA: ");
        console_write(ata_model());
        console_write("\n");
        if (ata_uses_bios_disk()) {
            console_write("Storage: BIOS boot disk active\n");
        } else if (ata_uses_ahci()) {
            console_write("Storage: AHCI SATA controller active\n");
        }
        console_write("FAT32: mounting boot disk...\n");
        int volume_mounted = fat32_mount();
        if (!volume_mounted) {
            console_write("FAT32: no supported volume mounted; continuing "
                          "without storage.\n");
        } else if (ata_uses_bios_disk()) {
            console_write("FAT32: BIOS boot volume mounted read-only; "
                          "writes disabled.\n");
        } else {
            console_write("FAT32: volume mounted; testing read/write...\n");
            if (!filesystem_self_test()) {
                console_write("FAT32 read/write test failed.\n");
                halt_forever();
            }
            console_write("FAT32: mount, CRUD and LFN test passed\n");
        }
    }

    report_rtl8192se_status();

    if (network_initialize()) {
        console_write("Network: ");
        console_write(network_interface_name());
        console_write(" ready\n");
    } else {
        console_write("Network: no supported interface\n");
    }

    pic_initialize();
    pit_initialize(100u);
    pic_unmask_irq(0u);
    keyboard_initialize();
    int mouse_available = mouse_initialize();
    console_write("PIC: remapped; PIT: 100 Hz\n");
    console_write("Keyboard: PS/2 input enabled\n");
    console_write(mouse_available ? "Mouse: PS/2 initialized\n"
                                  : "Mouse: PS/2 unavailable\n");

    __asm__ volatile ("sti");
    uint32_t initial_ticks = pit_ticks_read();
    while ((uint32_t)(pit_ticks_read() - initial_ticks) < 10u) {
        __asm__ volatile ("hlt");
    }

    console_write("PIT ticks: ");
    console_write_u32(pit_ticks_read());
    console_write("\n");

    if (thread_create(thread_a, 0) < 0 || thread_create(thread_b, 0) < 0) {
        console_write("Thread creation failed.\n");
        halt_forever();
    }
    uint32_t completed_threads = scheduler_run();
    if (completed_threads != 2u || thread_a_runs != 1u ||
        thread_b_runs != 1u) {
        console_write("Thread scheduler self-test failed.\n");
        halt_forever();
    }
    console_write("Threads: ");
    console_write_u32(completed_threads);
    console_write(" preemptive tasks completed\n");
    if (!user_mode_self_test()) {
        console_write("User-mode ring 3 self-test failed.\n");
        halt_forever();
    }
    console_write("User mode: syscall and kernel-page isolation tests passed\n");
    console_write("Kernel subsystems active (partial).\n");
    shell_run();
}
