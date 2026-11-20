#ifndef TEXUGOS_NETWORK_LINK_H
#define TEXUGOS_NETWORK_LINK_H

#include <stdint.h>

struct network_link {
    const char *name;
    int (*send)(const void *frame, uint16_t length);
    int (*receive)(void *frame, uint16_t capacity, uint16_t *length);
    void (*get_mac)(uint8_t address[6]);
};

const struct network_link *network_link_initialize(void);

#endif
