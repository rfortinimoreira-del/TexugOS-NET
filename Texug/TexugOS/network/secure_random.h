#ifndef TEXUGOS_SECURE_RANDOM_H
#define TEXUGOS_SECURE_RANDOM_H

#include <stdint.h>

int wpa2_secure_random_available(void);
int wpa2_secure_random_fill(void *context, uint8_t *buffer,
                            uint32_t length);

#endif
