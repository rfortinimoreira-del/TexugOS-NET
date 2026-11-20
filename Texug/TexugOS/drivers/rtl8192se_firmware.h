#ifndef TEXUGOS_RTL8192SE_FIRMWARE_H
#define TEXUGOS_RTL8192SE_FIRMWARE_H

#include <stdint.h>

struct rtl8192se_firmware {
    const uint8_t *bytes;
    uint32_t size;
    uint16_t version;
    uint32_t imem_offset;
    uint32_t imem_size;
    uint32_t emem_offset;
    uint32_t emem_size;
    uint32_t dmem_offset;
    uint32_t dmem_size;
};

int rtl8192se_firmware_load(struct rtl8192se_firmware *firmware);
int rtl8192se_firmware_prepare_dmem(
    const struct rtl8192se_firmware *firmware, uint8_t rf_configuration,
    uint8_t *buffer, uint32_t capacity);
int rtl8192se_firmware_build_download_descriptor(
    uint8_t descriptor[64], uint32_t buffer_address, uint32_t length,
    uint32_t next_descriptor_address, uint8_t last_init_packet);

#endif
