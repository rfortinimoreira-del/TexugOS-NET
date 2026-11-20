#include <stdint.h>

#include "wpa2_ccmp_frame.h"
#include "wpa2_crypto.h"

#define WLAN_DATA_HEADER_SIZE 24u
#define WLAN_QOS_HEADER_SIZE 26u
#define CCMP_HEADER_SIZE 8u
#define CCMP_MIC_SIZE 8u
#define CCMP_EXTENDED_IV 0x20u
#define CCMP_PACKET_NUMBER_MAX 0x0000ffffffffffffull

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       uint32_t length)
{
    for (uint32_t index = 0u; index < length; ++index) {
        destination[index] = source[index];
    }
}

static int get_header_size(const uint8_t *frame, uint32_t frame_length,
                           uint32_t *header_size)
{
    if (frame == 0 || header_size == 0 || frame_length < WLAN_DATA_HEADER_SIZE ||
        (frame[0] & 0x0cu) != 0x08u || (frame[0] & 0x03u) != 0u ||
        (frame[1] & 0x03u) == 0x03u || (frame[1] & 0x04u) != 0u ||
        (frame[1] & 0x80u) != 0u || (frame[22] & 0x0fu) != 0u) {
        return 0;
    }
    uint32_t subtype = (frame[0] >> 4) & 0x0fu;
    *header_size = subtype >= 8u ? WLAN_QOS_HEADER_SIZE
                                 : WLAN_DATA_HEADER_SIZE;
    if (frame_length < *header_size) {
        return 0;
    }
    return 1;
}

static int make_aad_nonce(const uint8_t *frame, uint32_t header_size,
                          uint8_t aad[30], uint32_t *aad_length,
                          uint8_t nonce[13])
{
    if (frame == 0 || aad == 0 || aad_length == 0 || nonce == 0 ||
        (header_size != WLAN_DATA_HEADER_SIZE &&
         header_size != WLAN_QOS_HEADER_SIZE)) {
        return 0;
    }
    aad[0] = frame[0] & 0x8fu;
    aad[1] = frame[1] & 0xc7u;
    copy_bytes(aad + 2u, frame + 4u, 18u);
    aad[20] = frame[22] & 0x0fu;
    aad[21] = 0u;
    if (header_size == WLAN_QOS_HEADER_SIZE) {
        aad[22] = frame[24] & 0x0fu;
        aad[23] = 0u;
        *aad_length = 24u;
        nonce[0] = frame[24] & 0x0fu;
    } else {
        *aad_length = 22u;
        nonce[0] = 0u;
    }
    copy_bytes(nonce + 1u, frame + 10u, 6u);
    return 1;
}

static uint64_t read_packet_number(const uint8_t *header)
{
    return ((uint64_t)header[7] << 40) | ((uint64_t)header[6] << 32) |
           ((uint64_t)header[5] << 24) | ((uint64_t)header[4] << 16) |
           ((uint64_t)header[1] << 8) | header[0];
}

static void write_packet_number(uint8_t *header, uint64_t packet_number)
{
    header[0] = (uint8_t)packet_number;
    header[1] = (uint8_t)(packet_number >> 8);
    header[4] = (uint8_t)(packet_number >> 16);
    header[5] = (uint8_t)(packet_number >> 24);
    header[6] = (uint8_t)(packet_number >> 32);
    header[7] = (uint8_t)(packet_number >> 40);
}

int wpa2_ccmp_protect_frame(uint8_t *frame, uint32_t *frame_length,
                            uint32_t frame_capacity,
                            const uint8_t temporal_key[16], uint8_t key_id,
                            uint64_t *packet_number)
{
    uint32_t header_size;
    if (frame == 0 || frame_length == 0 || temporal_key == 0 ||
        packet_number == 0 || key_id > 3u ||
        !get_header_size(frame, *frame_length, &header_size) ||
        (frame[1] & 0x40u) != 0u ||
        *frame_length > frame_capacity ||
        frame_capacity - *frame_length < CCMP_HEADER_SIZE + CCMP_MIC_SIZE ||
        *packet_number >= CCMP_PACKET_NUMBER_MAX) {
        return 0;
    }

    uint64_t next_packet_number = *packet_number + 1u;
    uint32_t data_length = *frame_length - header_size;
    for (uint32_t index = *frame_length; index > header_size; --index) {
        frame[index + CCMP_HEADER_SIZE - 1u] = frame[index - 1u];
    }
    uint8_t *ccmp_header = frame + header_size;
    for (uint32_t index = 0u; index < CCMP_HEADER_SIZE; ++index) {
        ccmp_header[index] = 0u;
    }
    write_packet_number(ccmp_header, next_packet_number);
    ccmp_header[3] = (uint8_t)((key_id << 6) | CCMP_EXTENDED_IV);
    frame[1] |= 0x40u;

    uint8_t aad[30];
    uint32_t aad_length = 0u;
    uint8_t nonce[13];
    if (!make_aad_nonce(frame, header_size, aad, &aad_length, nonce)) {
        frame[1] &= (uint8_t)~0x40u;
        return 0;
    }
    for (uint32_t index = 0u; index < 6u; ++index) {
        nonce[7u + index] =
            (uint8_t)(next_packet_number >> ((5u - index) * 8u));
    }
    uint8_t mic[CCMP_MIC_SIZE];
    uint8_t *data = ccmp_header + CCMP_HEADER_SIZE;
    if (!wpa2_ccmp_encrypt(temporal_key, nonce, aad, aad_length, data,
                           data_length, mic)) {
        frame[1] &= (uint8_t)~0x40u;
        return 0;
    }
    copy_bytes(data + data_length, mic, sizeof(mic));
    *frame_length += CCMP_HEADER_SIZE + CCMP_MIC_SIZE;
    *packet_number = next_packet_number;
    return 1;
}

int wpa2_ccmp_unprotect_frame(uint8_t *frame, uint32_t *frame_length,
                              uint32_t frame_capacity,
                              const uint8_t pairwise_key[16],
                              const uint8_t group_key[16],
                              uint8_t group_key_id,
                              uint64_t *last_pairwise_packet_number,
                              uint64_t *last_group_packet_number)
{
    uint32_t header_size;
    if (frame == 0 || frame_length == 0 || pairwise_key == 0 ||
        group_key == 0 || last_pairwise_packet_number == 0 ||
        last_group_packet_number == 0 || group_key_id > 3u ||
        *frame_length > frame_capacity ||
        !get_header_size(frame, *frame_length, &header_size) ||
        (frame[1] & 0x40u) == 0u ||
        *frame_length < header_size + CCMP_HEADER_SIZE + CCMP_MIC_SIZE) {
        return 0;
    }
    uint8_t *ccmp_header = frame + header_size;
    if (ccmp_header[2] != 0u ||
        (ccmp_header[3] & 0x3fu) != CCMP_EXTENDED_IV) {
        return 0;
    }
    uint8_t multicast = (uint8_t)(frame[4] & 1u);
    uint8_t key_id = ccmp_header[3] >> 6;
    const uint8_t *key;
    uint64_t *last_packet_number;
    if (multicast != 0u) {
        if (key_id != group_key_id) {
            return 0;
        }
        key = group_key;
        last_packet_number = last_group_packet_number;
    } else {
        if (key_id != 0u) {
            return 0;
        }
        key = pairwise_key;
        last_packet_number = last_pairwise_packet_number;
    }
    uint64_t packet_number = read_packet_number(ccmp_header);
    if (packet_number == 0u || packet_number <= *last_packet_number) {
        return 0;
    }

    uint8_t aad[30];
    uint32_t aad_length = 0u;
    uint8_t nonce[13];
    if (!make_aad_nonce(frame, header_size, aad, &aad_length, nonce)) {
        return 0;
    }
    for (uint32_t index = 0u; index < 6u; ++index) {
        nonce[7u + index] =
            (uint8_t)(packet_number >> ((5u - index) * 8u));
    }
    uint32_t encrypted_length =
        *frame_length - header_size - CCMP_HEADER_SIZE;
    uint32_t data_length = encrypted_length - CCMP_MIC_SIZE;
    uint8_t *data = ccmp_header + CCMP_HEADER_SIZE;
    const uint8_t *mic = data + data_length;
    if (!wpa2_ccmp_decrypt(key, nonce, aad, aad_length, data, data_length,
                           mic)) {
        return 0;
    }
    for (uint32_t index = 0u; index < data_length; ++index) {
        frame[header_size + index] = data[index];
    }
    *frame_length -= CCMP_HEADER_SIZE + CCMP_MIC_SIZE;
    frame[1] &= (uint8_t)~0x40u;
    *last_packet_number = packet_number;
    return 1;
}
