#ifndef TEXUGOS_TEF_H
#define TEXUGOS_TEF_H

#include <stdint.h>

#define TEF_VERSION 1u
#define TEF_NAME_MAX 31u
#define TEF_ICON_WIDTH 8u
#define TEF_ICON_HEIGHT 8u
#define TEF_FIXED_HEADER_SIZE 24u
#define TEF_ICON_SIZE TEF_ICON_HEIGHT
#define TEF_BINARY_MAX_SIZE 4096u
#define TEF_FILE_MAX_SIZE \
    (TEF_FIXED_HEADER_SIZE + TEF_NAME_MAX + TEF_ICON_SIZE + \
     TEF_BINARY_MAX_SIZE)

struct tef_executable {
    const uint8_t *name;
    uint16_t name_length;
    const uint8_t *icon_rows;
    const uint8_t *binary;
    uint32_t binary_size;
    uint32_t entry_offset;
};

int tef_parse(const uint8_t *bytes, uint32_t size,
              struct tef_executable *executable);

#endif
