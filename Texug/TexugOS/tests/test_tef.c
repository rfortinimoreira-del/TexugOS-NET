#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tef.h"

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

int main(void)
{
    uint8_t bytes[64] = {0};
    const uint8_t binary[] = {0xb8u, 0x02u, 0u, 0u, 0u, 0xcdu, 0x80u};
    const uint8_t name[] = {'D', 'e', 'm', 'o'};
    memcpy(bytes, "TEF\0", 4u);
    write_le16(bytes + 4u, TEF_VERSION);
    write_le16(bytes + 6u,
               TEF_FIXED_HEADER_SIZE + sizeof(name) + TEF_ICON_SIZE);
    write_le16(bytes + 8u, sizeof(name));
    bytes[10] = TEF_ICON_WIDTH;
    bytes[11] = TEF_ICON_HEIGHT;
    write_le32(bytes + 12u, sizeof(binary));
    write_le32(bytes + 16u, 0u);
    memcpy(bytes + TEF_FIXED_HEADER_SIZE, name, sizeof(name));
    bytes[TEF_FIXED_HEADER_SIZE + sizeof(name)] = 0x3cu;
    memcpy(bytes + TEF_FIXED_HEADER_SIZE + sizeof(name) + TEF_ICON_SIZE,
           binary, sizeof(binary));

    struct tef_executable executable;
    uint32_t valid_size = TEF_FIXED_HEADER_SIZE + sizeof(name) +
                          TEF_ICON_SIZE + sizeof(binary);
    if (!tef_parse(bytes, valid_size, &executable) ||
        executable.name_length != sizeof(name) ||
        memcmp(executable.name, name, sizeof(name)) != 0 ||
        executable.icon_rows[0] != 0x3cu ||
        executable.binary_size != sizeof(binary) ||
        executable.entry_offset != 0u ||
        memcmp(executable.binary, binary, sizeof(binary)) != 0) {
        fputs("TEF valid executable parse failed\n", stderr);
        return 1;
    }

    uint8_t saved = bytes[0];
    bytes[0] = 'X';
    if (tef_parse(bytes, valid_size, &executable)) {
        fputs("TEF accepted an invalid magic value\n", stderr);
        return 1;
    }
    bytes[0] = saved;

    write_le32(bytes + 16u, sizeof(binary));
    if (tef_parse(bytes, valid_size, &executable)) {
        fputs("TEF accepted an entry point outside the binary\n", stderr);
        return 1;
    }
    write_le32(bytes + 16u, 0u);

    bytes[TEF_FIXED_HEADER_SIZE] = '\n';
    if (tef_parse(bytes, valid_size, &executable)) {
        fputs("TEF accepted a non-printable application name\n", stderr);
        return 1;
    }
    bytes[TEF_FIXED_HEADER_SIZE] = name[0];

    if (tef_parse(bytes, valid_size - 1u, &executable)) {
        fputs("TEF accepted a truncated binary\n", stderr);
        return 1;
    }
    return 0;
}
