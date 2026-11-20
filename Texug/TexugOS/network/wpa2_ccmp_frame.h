#ifndef TEXUGOS_WPA2_CCMP_FRAME_H
#define TEXUGOS_WPA2_CCMP_FRAME_H

#include <stdint.h>

int wpa2_ccmp_protect_frame(uint8_t *frame, uint32_t *frame_length,
                            uint32_t frame_capacity,
                            const uint8_t temporal_key[16], uint8_t key_id,
                            uint64_t *packet_number);
int wpa2_ccmp_unprotect_frame(uint8_t *frame, uint32_t *frame_length,
                              uint32_t frame_capacity,
                              const uint8_t pairwise_key[16],
                              const uint8_t group_key[16],
                              uint8_t group_key_id,
                              uint64_t *last_pairwise_packet_number,
                              uint64_t *last_group_packet_number);

#endif
