#include <stdint.h>

#include "fat32.h"
#include "rtl8192se_firmware.h"
#include "rtl8192se_firmware_blob.h"

#define RTL8192SE_FIRMWARE_NAME "RTL8192.BIN"
#define RTL8192SE_FIRMWARE_CAPACITY 90000u
#define RTL8192SE_FIRMWARE_HEADER_SIZE 80u
#define RTL8192SE_FIRMWARE_PRIVATE_OFFSET 32u
#define RTL8192SE_FIRMWARE_PRIVATE_SIZE \
    (RTL8192SE_FIRMWARE_HEADER_SIZE - RTL8192SE_FIRMWARE_PRIVATE_OFFSET)
#define RTL8192SE_FIRMWARE_SEGMENT_MAX 64000u
#define RTL8192SE_FIRMWARE_SIGNATURE 0x8192u

static uint8_t firmware_bytes[RTL8192SE_FIRMWARE_CAPACITY]
    __attribute__((aligned(16)));

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int parse_firmware(struct rtl8192se_firmware *firmware,
                          uint32_t size)
{
    if (size < RTL8192SE_FIRMWARE_HEADER_SIZE ||
        read_le16(firmware_bytes) != RTL8192SE_FIRMWARE_SIGNATURE) {
        return 0;
    }

    uint32_t dmem_size = read_le32(&firmware_bytes[4u]);
    uint32_t imem_size = read_le32(&firmware_bytes[8u]);
    uint32_t emem_size = read_le32(&firmware_bytes[12u]);
    uint32_t private_size = read_le32(&firmware_bytes[16u]);
    if (imem_size == 0u || imem_size > RTL8192SE_FIRMWARE_SEGMENT_MAX ||
        emem_size > RTL8192SE_FIRMWARE_SEGMENT_MAX ||
        dmem_size != RTL8192SE_FIRMWARE_PRIVATE_SIZE ||
        private_size != dmem_size ||
        imem_size > size - RTL8192SE_FIRMWARE_HEADER_SIZE ||
        emem_size > size - RTL8192SE_FIRMWARE_HEADER_SIZE - imem_size ||
        size - RTL8192SE_FIRMWARE_HEADER_SIZE - imem_size - emem_size !=
            dmem_size) {
        return 0;
    }

    firmware->bytes = firmware_bytes;
    firmware->size = size;
    firmware->version = read_le16(&firmware_bytes[2u]);
    firmware->imem_offset = RTL8192SE_FIRMWARE_HEADER_SIZE;
    firmware->imem_size = imem_size;
    firmware->emem_offset =
        RTL8192SE_FIRMWARE_HEADER_SIZE + imem_size;
    firmware->emem_size = emem_size;
    firmware->dmem_offset = RTL8192SE_FIRMWARE_PRIVATE_OFFSET;
    firmware->dmem_size = private_size;
    return 1;
}

int rtl8192se_firmware_load(struct rtl8192se_firmware *firmware)
{
    if (firmware == 0) {
        return 0;
    }

    uint32_t root = fat32_root_cluster();
    if (root != 0u) {
        uint32_t size = 0u;
        if (fat32_read_file(root, RTL8192SE_FIRMWARE_NAME,
                            firmware_bytes, sizeof(firmware_bytes), &size) &&
            parse_firmware(firmware, size)) {
            return 1;
        }
    }

    uint32_t embedded_size = RTL8192SE_EMBEDDED_FIRMWARE_SIZE;
    if (embedded_size == 0u || embedded_size > sizeof(firmware_bytes)) {
        return 0;
    }
    for (uint32_t index = 0u; index < embedded_size; ++index) {
        firmware_bytes[index] = rtl8192se_embedded_firmware[index];
    }
    return parse_firmware(firmware, embedded_size);
}

int rtl8192se_firmware_prepare_dmem(
    const struct rtl8192se_firmware *firmware, uint8_t rf_configuration,
    uint8_t *buffer, uint32_t capacity)
{
    if (firmware == 0 || firmware->bytes == 0 || buffer == 0 ||
        firmware->dmem_size != RTL8192SE_FIRMWARE_PRIVATE_SIZE ||
        firmware->dmem_offset > firmware->size ||
        firmware->dmem_size > firmware->size - firmware->dmem_offset ||
        capacity < firmware->dmem_size ||
        (rf_configuration != 0x11u && rf_configuration != 0x12u &&
         rf_configuration != 0x22u)) {
        return 0;
    }

    for (uint32_t index = 0u; index < firmware->dmem_size; ++index) {
        buffer[index] = firmware->bytes[firmware->dmem_offset + index];
    }
    buffer[2u] = 1u;
    buffer[6u] = rf_configuration;
    return 1;
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

int rtl8192se_firmware_build_download_descriptor(
    uint8_t descriptor[64], uint32_t buffer_address, uint32_t length,
    uint32_t next_descriptor_address, uint8_t last_init_packet)
{
    if (descriptor == 0 || buffer_address == 0u || length == 0u ||
        length > RTL8192SE_FIRMWARE_SEGMENT_MAX ||
        (next_descriptor_address & 0x3fu) != 0u ||
        next_descriptor_address == 0u || last_init_packet > 1u) {
        return 0;
    }

    uint32_t next = (uint32_t)descriptor[36u] |
                    ((uint32_t)descriptor[37u] << 8) |
                    ((uint32_t)descriptor[38u] << 16) |
                    ((uint32_t)descriptor[39u] << 24);
    for (uint32_t index = 0u; index < 64u; ++index) {
        descriptor[index] = 0u;
    }

    uint32_t flags = length | (1u << 27) | (1u << 26) | (1u << 31);
    if (last_init_packet != 0u) {
        flags |= 1u << 28;
    }
    write_le32(&descriptor[0u], flags);
    write_le32(&descriptor[28u], length);
    write_le32(&descriptor[32u], buffer_address);
    write_le32(&descriptor[36u],
               next != 0u ? next : next_descriptor_address);
    return 1;
}
