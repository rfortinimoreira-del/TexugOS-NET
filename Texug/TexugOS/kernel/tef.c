#include "tef.h"

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

int tef_parse(const uint8_t *bytes, uint32_t size,
              struct tef_executable *executable)
{
    if (bytes == 0 || executable == 0 ||
        size < TEF_FIXED_HEADER_SIZE + TEF_ICON_SIZE ||
        bytes[0] != 'T' || bytes[1] != 'E' || bytes[2] != 'F' ||
        bytes[3] != 0u || read_le16(bytes + 4u) != TEF_VERSION) {
        return 0;
    }

    uint16_t header_size = read_le16(bytes + 6u);
    uint16_t name_length = read_le16(bytes + 8u);
    uint32_t binary_size = read_le32(bytes + 12u);
    uint32_t entry_offset = read_le32(bytes + 16u);
    uint32_t expected_header_size =
        TEF_FIXED_HEADER_SIZE + name_length + TEF_ICON_SIZE;
    if (name_length == 0u || name_length > TEF_NAME_MAX ||
        bytes[10] != TEF_ICON_WIDTH || bytes[11] != TEF_ICON_HEIGHT ||
        read_le32(bytes + 20u) != 0u ||
        header_size != expected_header_size ||
        binary_size == 0u || binary_size > TEF_BINARY_MAX_SIZE ||
        entry_offset >= binary_size ||
        size != (uint32_t)header_size + binary_size) {
        return 0;
    }
    for (uint32_t index = 0u; index < name_length; ++index) {
        if (bytes[TEF_FIXED_HEADER_SIZE + index] < ' ' ||
            bytes[TEF_FIXED_HEADER_SIZE + index] > '~') {
            return 0;
        }
    }

    executable->name = bytes + TEF_FIXED_HEADER_SIZE;
    executable->name_length = name_length;
    executable->icon_rows = executable->name + name_length;
    executable->binary = bytes + header_size;
    executable->binary_size = binary_size;
    executable->entry_offset = entry_offset;
    return 1;
}
