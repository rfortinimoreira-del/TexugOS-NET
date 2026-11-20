#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rtl8192se_firmware.h"
#include "rtl8192se_firmware_blob.h"

#define TEST_FIRMWARE_CAPACITY 256u
#define TEST_HEADER_SIZE 80u

static uint8_t test_bytes[TEST_FIRMWARE_CAPACITY];
static uint32_t test_size;
static int file_available = 1;
static uint32_t test_root_cluster = 2u;

uint32_t fat32_root_cluster(void)
{
    return test_root_cluster;
}

int fat32_read_file(uint32_t directory, const char *name, void *buffer,
                    uint32_t capacity, uint32_t *size)
{
    if (!file_available || directory != 2u || strcmp(name, "RTL8192.BIN") != 0 ||
        capacity < test_size) {
        return 0;
    }
    memcpy(buffer, test_bytes, test_size);
    *size = test_size;
    return 1;
}

static void write_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void make_valid_firmware(void)
{
    memset(test_bytes, 0, sizeof(test_bytes));
    write_le16(&test_bytes[0], 0x8192u);
    write_le16(&test_bytes[2], 0x7031u);
    write_le32(&test_bytes[4], 48u);
    write_le32(&test_bytes[8], 4u);
    write_le32(&test_bytes[12], 3u);
    write_le32(&test_bytes[16], 48u);
    for (uint32_t index = 0u; index < 55u; ++index) {
        test_bytes[TEST_HEADER_SIZE + index] = (uint8_t)(index + 1u);
    }
    test_size = TEST_HEADER_SIZE + 4u + 3u + 48u;
}

static int expect_invalid(void)
{
    struct rtl8192se_firmware firmware;
    if (RTL8192SE_EMBEDDED_FIRMWARE_SIZE != 0u) {
        return rtl8192se_firmware_load(&firmware) &&
               firmware.size == RTL8192SE_EMBEDDED_FIRMWARE_SIZE;
    }
    return !rtl8192se_firmware_load(&firmware);
}

int main(void)
{
    struct rtl8192se_firmware firmware;
    uint8_t private_data[48];
    make_valid_firmware();
    if (!rtl8192se_firmware_load(&firmware) ||
        firmware.bytes == 0 || firmware.size != test_size ||
        firmware.version != 0x7031u || firmware.imem_offset != 80u ||
        firmware.imem_size != 4u || firmware.emem_offset != 84u ||
        firmware.emem_size != 3u || firmware.dmem_offset != 32u ||
        firmware.dmem_size != 48u ||
        firmware.bytes[firmware.emem_offset] != test_bytes[84u]) {
        fputs("valid RTL8192SE firmware rejected or parsed incorrectly\n",
              stderr);
        return 1;
    }
    if (!rtl8192se_firmware_prepare_dmem(&firmware, 0x22u,
                                         private_data, sizeof(private_data)) ||
        private_data[2u] != 1u || private_data[6u] != 0x22u ||
        private_data[0u] != test_bytes[32u] ||
        private_data[7u] != test_bytes[39u] ||
        rtl8192se_firmware_prepare_dmem(&firmware, 0u, private_data,
                                        sizeof(private_data)) ||
        rtl8192se_firmware_prepare_dmem(&firmware, 0x22u, private_data, 47u)) {
        fputs("RTL8192SE PCIe firmware private segment setup failed\n",
              stderr);
        return 1;
    }
    uint8_t descriptor[64];
    if (!rtl8192se_firmware_build_download_descriptor(
            descriptor, 0x00123450u, 0x1234u, 0x00123000u, 1u) ||
        read_le32(&descriptor[0u]) !=
            (0x1234u | (1u << 27) | (1u << 26) | (1u << 28) | (1u << 31)) ||
        read_le32(&descriptor[28u]) != 0x1234u ||
        read_le32(&descriptor[32u]) != 0x00123450u ||
        read_le32(&descriptor[36u]) != 0x00123000u ||
        rtl8192se_firmware_build_download_descriptor(
            descriptor, 0u, 1u, 0x00123000u, 1u) ||
        rtl8192se_firmware_build_download_descriptor(
            descriptor, 0x00123450u, 1u, 0x00123001u, 1u)) {
        fputs("RTL8192SE firmware TX descriptor setup failed\n", stderr);
        return 1;
    }
    write_le32(&descriptor[36u], 0x00123040u);
    if (!rtl8192se_firmware_build_download_descriptor(
            descriptor, 0x00123450u, 32u, 0x00123000u, 0u) ||
        read_le32(&descriptor[36u]) != 0x00123040u) {
        fputs("RTL8192SE descriptor rebuild overwrote the ring link\n",
              stderr);
        return 1;
    }

    test_bytes[0] = 0u;
    if (!expect_invalid()) {
        fputs("invalid RTL8192SE firmware signature accepted\n", stderr);
        return 1;
    }
    make_valid_firmware();
    test_size = TEST_HEADER_SIZE - 1u;
    if (!expect_invalid()) {
        fputs("truncated RTL8192SE firmware accepted\n", stderr);
        return 1;
    }
    make_valid_firmware();
    write_le32(&test_bytes[8], TEST_FIRMWARE_CAPACITY);
    if (!expect_invalid()) {
        fputs("oversized RTL8192SE firmware segment accepted\n", stderr);
        return 1;
    }
    make_valid_firmware();
    test_size--;
    if (!expect_invalid()) {
        fputs("RTL8192SE firmware with an incorrect total size accepted\n",
              stderr);
        return 1;
    }
    make_valid_firmware();
    file_available = 0;
    test_root_cluster = 0u;
    if (RTL8192SE_EMBEDDED_FIRMWARE_SIZE != 0u) {
        if (!rtl8192se_firmware_load(&firmware) ||
            firmware.size != RTL8192SE_EMBEDDED_FIRMWARE_SIZE) {
            fputs("embedded RTL8192SE firmware fallback failed\n", stderr);
            return 1;
        }
    } else if (!expect_invalid()) {
        fputs("missing RTL8192SE firmware accepted\n", stderr);
        return 1;
    }
    return 0;
}
