#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wpa2_ccmp_frame.h"

static void make_data_frame(uint8_t *frame, uint32_t *frame_length,
                            const uint8_t destination[6])
{
    static const uint8_t ap[6] = {0x02u, 1u, 2u, 3u, 4u, 5u};
    static const uint8_t source[6] = {0x02u, 6u, 7u, 8u, 9u, 10u};
    static const uint8_t payload[] = {
        0xaau, 0xaau, 0x03u, 0u, 0u, 0u, 0x08u, 0u,
        1u, 2u, 3u, 4u, 5u, 6u, 7u,
    };
    memset(frame, 0, 24u);
    frame[0] = 0x08u;
    frame[1] = 0x02u;
    memcpy(frame + 4u, destination, 6u);
    memcpy(frame + 10u, ap, 6u);
    memcpy(frame + 16u, source, 6u);
    memcpy(frame + 24u, payload, sizeof(payload));
    *frame_length = 24u + sizeof(payload);
}

int main(void)
{
    static const uint8_t pairwise_key[16] = {
        0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u,
        8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u,
    };
    static const uint8_t group_key[16] = {
        16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u,
        24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u,
    };
    static const uint8_t station[6] = {0x02u, 1u, 2u, 3u, 4u, 5u};
    static const uint8_t multicast[6] = {0x01u, 0u, 0x5eu, 0u, 0u, 1u};
    uint8_t frame[128];
    uint8_t original[128];
    uint8_t replay_frame[128];
    uint32_t frame_length = 0u;
    uint64_t transmit_pn = 0u;
    uint64_t group_transmit_pn = 0u;
    uint64_t last_pairwise_pn = 0u;
    uint64_t last_group_pn = 0u;
    make_data_frame(frame, &frame_length, station);
    memcpy(original, frame, frame_length);
    int protected_ok = wpa2_ccmp_protect_frame(
        frame, &frame_length, sizeof(frame), pairwise_key, 0u, &transmit_pn);
    uint32_t replay_frame_length = frame_length;
    if (protected_ok) {
        memcpy(replay_frame, frame, frame_length);
    }
    int unprotected_ok = protected_ok && wpa2_ccmp_unprotect_frame(
        frame, &frame_length, sizeof(frame), pairwise_key, group_key, 1u,
        &last_pairwise_pn, &last_group_pn);
    if (!unprotected_ok || frame_length != 39u || last_pairwise_pn != 1u ||
        memcmp(frame, original, 39u) != 0) {
        fprintf(stderr,
                "WPA2 pairwise CCMP frame round-trip failed "
                "(protect=%d unprotect=%d length=%u pn=%llu)\n",
                protected_ok, unprotected_ok, frame_length,
                (unsigned long long)last_pairwise_pn);
        for (uint32_t index = 0u; index < 39u; ++index) {
            if (frame[index] != original[index]) {
                fprintf(stderr, "byte %u got %02x expected %02x\n", index,
                        frame[index], original[index]);
            }
        }
        return 1;
    }
    memcpy(frame, replay_frame, replay_frame_length);
    frame_length = replay_frame_length;
    if (wpa2_ccmp_unprotect_frame(frame, &frame_length, sizeof(frame),
                                  pairwise_key, group_key, 1u,
                                  &last_pairwise_pn, &last_group_pn)) {
        fputs("WPA2 CCMP accepted a replayed packet number\n", stderr);
        return 1;
    }

    make_data_frame(frame, &frame_length, station);
    if (!wpa2_ccmp_protect_frame(frame, &frame_length, sizeof(frame),
                                 pairwise_key, 0u, &transmit_pn)) {
        fputs("WPA2 CCMP tamper test setup failed\n", stderr);
        return 1;
    }
    frame[frame_length - 1u] ^= 1u;
    if (wpa2_ccmp_unprotect_frame(frame, &frame_length, sizeof(frame),
                                  pairwise_key, group_key, 1u,
                                  &last_pairwise_pn, &last_group_pn) ||
        last_pairwise_pn != 1u) {
        fputs("WPA2 CCMP accepted tampered data or advanced replay state\n",
              stderr);
        return 1;
    }

    make_data_frame(frame, &frame_length, multicast);
    if (!wpa2_ccmp_protect_frame(frame, &frame_length, sizeof(frame),
                                 group_key, 2u, &group_transmit_pn) ||
        !wpa2_ccmp_unprotect_frame(frame, &frame_length, sizeof(frame),
                                   pairwise_key, group_key, 2u,
                                   &last_pairwise_pn, &last_group_pn) ||
        last_group_pn != 1u) {
        fputs("WPA2 group CCMP frame round-trip failed\n", stderr);
        return 1;
    }
    return 0;
}
