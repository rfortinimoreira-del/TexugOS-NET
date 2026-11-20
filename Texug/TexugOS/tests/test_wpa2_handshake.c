#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wpa2_crypto.h"
#include "wpa2_handshake.h"

#define EAPOL_KEY_DATA_OFFSET 99u
#define EAPOL_KEY_MIC_OFFSET 81u

static int deterministic_random(void *context, uint8_t *buffer,
                               uint32_t length)
{
    uint8_t *next = (uint8_t *)context;
    for (uint32_t index = 0u; index < length; ++index) {
        buffer[index] = (*next)++;
    }
    return 1;
}

static int unavailable_random(void *context, uint8_t *buffer,
                             uint32_t length)
{
    (void)context;
    memset(buffer, 0xa5, length);
    return 0;
}

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void write_be64(uint8_t *bytes, uint64_t value)
{
    for (uint32_t index = 0u; index < 8u; ++index) {
        bytes[7u - index] = (uint8_t)(value >> (index * 8u));
    }
}

static int set_mic(uint8_t *eapol, uint32_t length, const uint8_t kck[16])
{
    uint8_t mic[16];
    if (!wpa2_eapol_mic(kck, eapol, length, mic)) {
        return 0;
    }
    for (uint32_t index = 0u; index < sizeof(mic); ++index) {
        eapol[EAPOL_KEY_MIC_OFFSET + index] = mic[index];
    }
    return 1;
}

static int verify_mic(const uint8_t *eapol, uint32_t length,
                      const uint8_t kck[16])
{
    uint8_t copy[WPA2_EAPOL_MAX_SIZE];
    uint8_t expected[16];
    uint8_t difference = 0u;
    if (length > sizeof(copy)) {
        return 0;
    }
    memcpy(copy, eapol, length);
    memset(copy + EAPOL_KEY_MIC_OFFSET, 0, sizeof(expected));
    if (!wpa2_eapol_mic(kck, copy, length, expected)) {
        return 0;
    }
    for (uint32_t index = 0u; index < sizeof(expected); ++index) {
        difference |= expected[index] ^ eapol[EAPOL_KEY_MIC_OFFSET + index];
    }
    return difference == 0u;
}

int main(void)
{
    static const uint8_t passphrase[] = "password";
    static const uint8_t ssid[] = "IEEE";
    static const uint8_t authenticator_address[6] = {
        0x00u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u,
    };
    static const uint8_t supplicant_address[6] = {
        0x66u, 0x77u, 0x88u, 0x99u, 0xaau, 0xbbu,
    };
    static const uint8_t gtk[16] = {
        0x10u, 0x21u, 0x32u, 0x43u, 0x54u, 0x65u, 0x76u, 0x87u,
        0x98u, 0xa9u, 0xbau, 0xcbu, 0xdcu, 0xedu, 0xfeu, 0x0fu,
    };
    struct wpa2_handshake handshake;
    uint8_t random_state = 1u;
    uint8_t response[WPA2_EAPOL_MAX_SIZE];
    uint32_t response_length = 0u;
    if (!wpa2_handshake_begin(&handshake, passphrase,
                              sizeof(passphrase) - 1u, ssid,
                              sizeof(ssid) - 1u, authenticator_address,
                              supplicant_address, deterministic_random,
                              &random_state)) {
        fputs("WPA2 handshake initialization failed\n", stderr);
        return 1;
    }

    uint8_t m1[EAPOL_KEY_DATA_OFFSET] = {0};
    m1[0] = 2u;
    m1[1] = 3u;
    write_be16(m1 + 2u, sizeof(m1) - 4u);
    m1[4] = 2u;
    write_be16(m1 + 5u, 0x008au);
    write_be16(m1 + 7u, 16u);
    write_be64(m1 + 9u, 1u);
    for (uint32_t index = 0u; index < 32u; ++index) {
        m1[17u + index] = (uint8_t)(0x80u + index);
    }
    if (wpa2_handshake_process(&handshake, m1, sizeof(m1), response,
                               sizeof(response), &response_length) !=
            WPA2_HANDSHAKE_RESPONSE ||
        response_length != EAPOL_KEY_DATA_OFFSET + 22u ||
        handshake.state != WPA2_HANDSHAKE_WAIT_M3 ||
        response[4] != 2u || response[1] != 3u ||
        read_be16(response + 5u) != 0x010au ||
        read_be16(response + 97u) != 22u ||
        !verify_mic(response, response_length, handshake.ptk)) {
        fputs("WPA2 M1/M2 exchange test failed\n", stderr);
        return 1;
    }

    uint8_t plaintext_kde[24] = {
        221u, 22u, 0u, 0x0fu, 0xacu, 1u, 5u, 0u,
    };
    memcpy(plaintext_kde + 8u, gtk, sizeof(gtk));
    uint8_t wrapped_kde[32];
    uint32_t wrapped_kde_length = 0u;
    if (!wpa2_aes_wrap(handshake.ptk + 16u, plaintext_kde,
                       sizeof(plaintext_kde), wrapped_kde,
                       sizeof(wrapped_kde), &wrapped_kde_length) ||
        wrapped_kde_length != sizeof(wrapped_kde)) {
        fputs("WPA2 GTK wrapping test setup failed\n", stderr);
        return 1;
    }
    uint8_t m3[EAPOL_KEY_DATA_OFFSET + sizeof(wrapped_kde)] = {0};
    m3[0] = 2u;
    m3[1] = 3u;
    write_be16(m3 + 2u, sizeof(m3) - 4u);
    m3[4] = 2u;
    write_be16(m3 + 5u, 0x13cau);
    write_be16(m3 + 7u, 16u);
    write_be64(m3 + 9u, 2u);
    memcpy(m3 + 17u, m1 + 17u, 32u);
    write_be16(m3 + 97u, sizeof(wrapped_kde));
    memcpy(m3 + EAPOL_KEY_DATA_OFFSET, wrapped_kde, sizeof(wrapped_kde));
    if (!set_mic(m3, sizeof(m3), handshake.ptk) ||
        wpa2_handshake_process(&handshake, m3, sizeof(m3), response,
                               sizeof(response), &response_length) !=
            WPA2_HANDSHAKE_COMPLETE ||
        handshake.state != WPA2_HANDSHAKE_ESTABLISHED ||
        handshake.gtk_valid == 0u || handshake.gtk_id != 1u ||
        memcmp(handshake.gtk, gtk, sizeof(gtk)) != 0 ||
        response_length != EAPOL_KEY_DATA_OFFSET ||
        read_be16(response + 5u) != 0x030au ||
        !verify_mic(response, response_length, handshake.ptk)) {
        fputs("WPA2 M3/M4 exchange or GTK installation test failed\n", stderr);
        return 1;
    }

    uint8_t duplicate_m4[WPA2_EAPOL_MAX_SIZE];
    uint32_t duplicate_m4_length = 0u;
    if (wpa2_handshake_process(&handshake, m3, sizeof(m3), duplicate_m4,
                               sizeof(duplicate_m4),
                               &duplicate_m4_length) !=
            WPA2_HANDSHAKE_COMPLETE ||
        duplicate_m4_length != response_length ||
        memcmp(duplicate_m4, response, response_length) != 0) {
        fputs("WPA2 duplicate M3 retransmission test failed\n", stderr);
        return 1;
    }

    uint8_t rekey_m1[EAPOL_KEY_DATA_OFFSET] = {0};
    rekey_m1[0] = 2u;
    rekey_m1[1] = 3u;
    write_be16(rekey_m1 + 2u, sizeof(rekey_m1) - 4u);
    rekey_m1[4] = 2u;
    write_be16(rekey_m1 + 5u, 0x008au);
    write_be16(rekey_m1 + 7u, 16u);
    write_be64(rekey_m1 + 9u, 3u);
    for (uint32_t index = 0u; index < 32u; ++index) {
        rekey_m1[17u + index] = (uint8_t)(0x40u + index);
    }
    if (wpa2_handshake_process(&handshake, rekey_m1, sizeof(rekey_m1),
                               response, sizeof(response),
                               &response_length) != WPA2_HANDSHAKE_RESPONSE ||
        handshake.state != WPA2_HANDSHAKE_WAIT_M3_REKEY ||
        handshake.ptk_generation != 1u ||
        !verify_mic(response, response_length, handshake.pending_ptk)) {
        fputs("WPA2 pairwise rekey M1/M2 exchange test failed\n", stderr);
        return 1;
    }
    uint8_t rekey_snonce[32];
    memcpy(rekey_snonce, handshake.supplicant_nonce, sizeof(rekey_snonce));
    if (wpa2_handshake_process(&handshake, rekey_m1, sizeof(rekey_m1),
                               duplicate_m4, sizeof(duplicate_m4),
                               &duplicate_m4_length) !=
            WPA2_HANDSHAKE_RESPONSE ||
        duplicate_m4_length != response_length ||
        memcmp(duplicate_m4, response, response_length) != 0 ||
        memcmp(rekey_snonce, handshake.supplicant_nonce,
               sizeof(rekey_snonce)) != 0) {
        fputs("WPA2 duplicate pairwise rekey M1 retransmission test failed\n",
              stderr);
        return 1;
    }

    uint8_t rekey_plaintext_kde[24] = {
        221u, 22u, 0u, 0x0fu, 0xacu, 1u, 6u, 0u,
    };
    memcpy(rekey_plaintext_kde + 8u, gtk, sizeof(gtk));
    uint8_t rekey_wrapped_kde[32];
    uint32_t rekey_wrapped_kde_length = 0u;
    if (!wpa2_aes_wrap(handshake.pending_ptk + 16u, rekey_plaintext_kde,
                       sizeof(rekey_plaintext_kde), rekey_wrapped_kde,
                       sizeof(rekey_wrapped_kde),
                       &rekey_wrapped_kde_length)) {
        fputs("WPA2 pairwise rekey wrapping test setup failed\n", stderr);
        return 1;
    }
    uint8_t rekey_m3[EAPOL_KEY_DATA_OFFSET + sizeof(rekey_wrapped_kde)] = {0};
    rekey_m3[0] = 2u;
    rekey_m3[1] = 3u;
    write_be16(rekey_m3 + 2u, sizeof(rekey_m3) - 4u);
    rekey_m3[4] = 2u;
    write_be16(rekey_m3 + 5u, 0x13cau);
    write_be16(rekey_m3 + 7u, 16u);
    write_be64(rekey_m3 + 9u, 4u);
    memcpy(rekey_m3 + 17u, rekey_m1 + 17u, 32u);
    write_be16(rekey_m3 + 97u, (uint16_t)rekey_wrapped_kde_length);
    memcpy(rekey_m3 + EAPOL_KEY_DATA_OFFSET, rekey_wrapped_kde,
           rekey_wrapped_kde_length);
    uint8_t installed_rekey_ptk[64];
    memcpy(installed_rekey_ptk, handshake.pending_ptk,
           sizeof(installed_rekey_ptk));
    if (!set_mic(rekey_m3, sizeof(rekey_m3), installed_rekey_ptk) ||
        wpa2_handshake_process(&handshake, rekey_m3, sizeof(rekey_m3),
                               response, sizeof(response),
                               &response_length) !=
            WPA2_HANDSHAKE_COMPLETE ||
        handshake.state != WPA2_HANDSHAKE_ESTABLISHED ||
        handshake.ptk_generation != 2u ||
        memcmp(handshake.ptk, installed_rekey_ptk,
               sizeof(installed_rekey_ptk)) != 0 ||
        read_be16(response + 5u) != 0x030au ||
        !verify_mic(response, response_length, handshake.ptk)) {
        fputs("WPA2 pairwise rekey M3/M4 key installation test failed\n",
              stderr);
        return 1;
    }
    if (wpa2_handshake_process(&handshake, rekey_m3, sizeof(rekey_m3),
                               duplicate_m4, sizeof(duplicate_m4),
                               &duplicate_m4_length) !=
            WPA2_HANDSHAKE_COMPLETE ||
        duplicate_m4_length != response_length ||
        memcmp(duplicate_m4, response, response_length) != 0 ||
        handshake.ptk_generation != 2u) {
        fputs("WPA2 duplicate pairwise rekey M3 retransmission test failed\n",
              stderr);
        return 1;
    }

    static const uint8_t renewed_gtk[16] = {
        0x20u, 0x31u, 0x42u, 0x53u, 0x64u, 0x75u, 0x86u, 0x97u,
        0xa8u, 0xb9u, 0xcau, 0xdbu, 0xecu, 0xfdu, 0x0eu, 0x1fu,
    };
    memcpy(plaintext_kde + 8u, renewed_gtk, sizeof(renewed_gtk));
    if (!wpa2_aes_wrap(handshake.ptk + 16u, plaintext_kde,
                       sizeof(plaintext_kde), wrapped_kde,
                       sizeof(wrapped_kde), &wrapped_kde_length)) {
        fputs("WPA2 group-key wrapping test setup failed\n", stderr);
        return 1;
    }
    uint8_t group_m1[EAPOL_KEY_DATA_OFFSET + sizeof(wrapped_kde)] = {0};
    group_m1[0] = 2u;
    group_m1[1] = 3u;
    write_be16(group_m1 + 2u, sizeof(group_m1) - 4u);
    group_m1[4] = 2u;
    write_be16(group_m1 + 5u, 0x1382u);
    write_be16(group_m1 + 7u, 16u);
    write_be64(group_m1 + 9u, 5u);
    write_be16(group_m1 + 97u, sizeof(wrapped_kde));
    memcpy(group_m1 + EAPOL_KEY_DATA_OFFSET, wrapped_kde,
           sizeof(wrapped_kde));
    if (!set_mic(group_m1, sizeof(group_m1), handshake.ptk) ||
        wpa2_handshake_process(&handshake, group_m1, sizeof(group_m1),
                               response, sizeof(response),
                               &response_length) !=
            WPA2_HANDSHAKE_RESPONSE ||
        handshake.gtk_generation != 3u || handshake.gtk_id != 1u ||
        memcmp(handshake.gtk, renewed_gtk, sizeof(renewed_gtk)) != 0 ||
        response_length != EAPOL_KEY_DATA_OFFSET ||
        read_be16(response + 5u) != 0x0302u ||
        !verify_mic(response, response_length, handshake.ptk)) {
        fputs("WPA2 group-key rekey exchange test failed\n", stderr);
        return 1;
    }
    if (wpa2_handshake_process(&handshake, group_m1, sizeof(group_m1),
                               duplicate_m4, sizeof(duplicate_m4),
                               &duplicate_m4_length) !=
            WPA2_HANDSHAKE_RESPONSE ||
        duplicate_m4_length != response_length ||
        memcmp(duplicate_m4, response, response_length) != 0) {
        fputs("WPA2 duplicate group-key message test failed\n", stderr);
        return 1;
    }
    m3[EAPOL_KEY_MIC_OFFSET] ^= 1u;
    if (wpa2_handshake_process(&handshake, m3, sizeof(m3), response,
                               sizeof(response), &response_length) !=
        WPA2_HANDSHAKE_ERROR) {
        fputs("WPA2 handshake accepted a tampered M3 MIC\n", stderr);
        return 1;
    }
    wpa2_handshake_clear(&handshake);

    struct wpa2_handshake fallback_handshake;
    if (!wpa2_handshake_begin(&fallback_handshake, passphrase,
                              sizeof(passphrase) - 1u, ssid,
                              sizeof(ssid) - 1u, authenticator_address,
                              supplicant_address, unavailable_random, 0) ||
        wpa2_handshake_process(&fallback_handshake, m1, sizeof(m1), response,
                               sizeof(response), &response_length) !=
            WPA2_HANDSHAKE_RESPONSE) {
        fputs("WPA2 SNonce fallback did not start without hardware RNG\n",
              stderr);
        return 1;
    }
    uint8_t expected_snonce[32];
    if (!wpa2_derive_snonce(
            fallback_handshake.pmk, authenticator_address,
            supplicant_address, m1 + 17u, m1 + 9u, 0u, expected_snonce) ||
        memcmp(fallback_handshake.supplicant_nonce, expected_snonce,
               sizeof(expected_snonce)) != 0 ||
        !verify_mic(response, response_length, fallback_handshake.ptk)) {
        fputs("WPA2 keyed SNonce fallback derivation test failed\n", stderr);
        return 1;
    }
    wpa2_handshake_clear(&fallback_handshake);

    struct wpa2_handshake changed_nonce_handshake;
    uint8_t changed_m1[sizeof(m1)];
    memcpy(changed_m1, m1, sizeof(changed_m1));
    changed_m1[17] ^= 0x80u;
    write_be64(changed_m1 + 9u, 9u);
    if (!wpa2_handshake_begin(&changed_nonce_handshake, passphrase,
                              sizeof(passphrase) - 1u, ssid,
                              sizeof(ssid) - 1u, authenticator_address,
                              supplicant_address, unavailable_random, 0) ||
        wpa2_handshake_process(&changed_nonce_handshake, changed_m1,
                               sizeof(changed_m1), response, sizeof(response),
                               &response_length) !=
            WPA2_HANDSHAKE_RESPONSE ||
        memcmp(changed_nonce_handshake.supplicant_nonce, expected_snonce,
               sizeof(expected_snonce)) == 0) {
        fputs("WPA2 keyed SNonce did not change with AP handshake input\n",
              stderr);
        return 1;
    }
    wpa2_handshake_clear(&changed_nonce_handshake);
    return 0;
}
