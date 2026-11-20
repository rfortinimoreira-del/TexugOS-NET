#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wpa2_crypto.h"

static int matches_hex(const uint8_t *bytes, const char *hex, uint32_t length)
{
    static const char digits[] = "0123456789abcdef";
    for (uint32_t index = 0u; index < length; ++index) {
        if (digits[bytes[index] >> 4] != hex[index * 2u] ||
            digits[bytes[index] & 0x0fu] != hex[index * 2u + 1u]) {
            return 0;
        }
    }
    return hex[length * 2u] == '\0';
}

int main(void)
{
    uint8_t pmk[32];
    uint8_t ptk[64];
    uint8_t authenticator_address[6] = {0, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t supplicant_address[6] = {0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};
    uint8_t authenticator_nonce[32];
    uint8_t supplicant_nonce[32];
    static const uint8_t password[] = "password";
    static const uint8_t ssid[] = "IEEE";
    if (!wpa2_derive_pmk(password, sizeof(password) - 1u,
                         ssid, sizeof(ssid) - 1u, pmk) ||
        !matches_hex(pmk,
                     "f42c6fc52df0ebef9ebb4b90b38a5f90"
                     "2e83fe1b135a70e23aed762e9710a12e",
                     sizeof(pmk))) {
        fputs("WPA2 PBKDF2 known-answer test failed\n", stderr);
        return 1;
    }

    for (uint32_t index = 0u; index < 32u; ++index) {
        authenticator_nonce[index] = (uint8_t)index;
        supplicant_nonce[index] = (uint8_t)(index + 32u);
    }
    for (uint32_t index = 0u; index < sizeof(pmk); ++index) {
        pmk[index] = (uint8_t)index;
    }
    if (!wpa2_derive_ptk(pmk, authenticator_address, supplicant_address,
                         authenticator_nonce, supplicant_nonce, ptk) ||
        !matches_hex(ptk,
                     "79c315817da2ce917e3264eeac39f908"
                     "edafe78205ab10e5fa4d85505235f696"
                     "ad89092024c34524f07b21e1f7baf3b"
                     "476e2b3f064f810eb140708be968d02f3",
                     sizeof(ptk))) {
        fputs("WPA2 PTK known-answer test failed\n", stderr);
        return 1;
    }

    static const uint8_t kck[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };
    static const uint8_t eapol[] =
        "example eapol key packet with mic field zeroed";
    uint8_t mic[16];
    if (!wpa2_eapol_mic(kck, eapol, sizeof(eapol) - 1u, mic) ||
        !matches_hex(mic, "d45dd0f766ff9f4942fefac9800d07ac", sizeof(mic))) {
        fputs("WPA2 EAPOL MIC known-answer test failed\n", stderr);
        return 1;
    }

    static const uint8_t ccmp_key[16] = {
        0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
        0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf
    };
    static const uint8_t ccmp_nonce[13] = {
        0, 0, 0, 6, 5, 4, 3, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5
    };
    static const uint8_t ccmp_aad[12] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
    };
    static const uint8_t expected_ciphertext[19] = {
        0xa2, 0x8c, 0x68, 0x65, 0x93, 0x9a, 0x9a, 0x79,
        0xfa, 0xaa, 0x5c, 0x4c, 0x2a, 0x9d, 0x4a, 0x91,
        0xcd, 0xac, 0x8c
    };
    static const uint8_t expected_mic[8] = {
        0x96, 0xc8, 0x61, 0xb9, 0xc9, 0xe6, 0x1e, 0xf1
    };
    uint8_t packet[19];
    for (uint32_t index = 0u; index < sizeof(packet); ++index) {
        packet[index] = (uint8_t)(index + 12u);
    }
    uint8_t ccmp_mic[8];
    if (!wpa2_ccmp_encrypt(ccmp_key, ccmp_nonce, ccmp_aad,
                           sizeof(ccmp_aad), packet, sizeof(packet),
                           ccmp_mic) ||
        memcmp(packet, expected_ciphertext, sizeof(packet)) != 0 ||
        memcmp(ccmp_mic, expected_mic, sizeof(ccmp_mic)) != 0 ||
        !wpa2_ccmp_decrypt(ccmp_key, ccmp_nonce, ccmp_aad,
                           sizeof(ccmp_aad), packet, sizeof(packet),
                           ccmp_mic)) {
        fputs("WPA2 CCMP RFC 3610 known-answer test failed\n", stderr);
        return 1;
    }
    for (uint32_t index = 0u; index < sizeof(packet); ++index) {
        if (packet[index] != (uint8_t)(index + 12u)) {
            fputs("WPA2 CCMP decryption did not restore plaintext\n", stderr);
            return 1;
        }
    }

    static const uint8_t unwrap_kek[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    static const uint8_t wrapped_key[24] = {
        0x1f, 0xa6, 0x8b, 0x0a, 0x81, 0x12, 0xb4, 0x47,
        0xae, 0xf3, 0x4b, 0xd8, 0xfb, 0x5a, 0x7b, 0x82,
        0x9d, 0x3e, 0x86, 0x23, 0x71, 0xd2, 0xcf, 0xe5,
    };
    uint8_t unwrapped_key[16];
    uint32_t unwrapped_length = 0u;
    if (!wpa2_aes_unwrap(unwrap_kek, wrapped_key, sizeof(wrapped_key),
                         unwrapped_key, sizeof(unwrapped_key),
                         &unwrapped_length) ||
        unwrapped_length != sizeof(unwrapped_key) ||
        !matches_hex(unwrapped_key, "00112233445566778899aabbccddeeff",
                     sizeof(unwrapped_key))) {
        fputs("WPA2 AES key unwrap known-answer test failed\n", stderr);
        return 1;
    }
    uint8_t wrapped_again[24];
    uint32_t wrapped_again_length = 0u;
    if (!wpa2_aes_wrap(unwrap_kek, unwrapped_key, sizeof(unwrapped_key),
                       wrapped_again, sizeof(wrapped_again),
                       &wrapped_again_length) ||
        wrapped_again_length != sizeof(wrapped_key) ||
        memcmp(wrapped_again, wrapped_key, sizeof(wrapped_key)) != 0) {
        fputs("WPA2 AES key wrap known-answer test failed\n", stderr);
        return 1;
    }
    uint8_t tampered_wrap[sizeof(wrapped_key)];
    for (uint32_t index = 0u; index < sizeof(wrapped_key); ++index) {
        tampered_wrap[index] = wrapped_key[index];
    }
    tampered_wrap[0] ^= 1u;
    if (wpa2_aes_unwrap(unwrap_kek, tampered_wrap, sizeof(tampered_wrap),
                        unwrapped_key, sizeof(unwrapped_key),
                        &unwrapped_length)) {
        fputs("WPA2 AES key unwrap accepted an invalid integrity value\n",
              stderr);
        return 1;
    }

    for (uint32_t index = 0u; index < sizeof(packet); ++index) {
        packet[index] = (uint8_t)(index + 12u);
    }
    if (!wpa2_ccmp_encrypt(ccmp_key, ccmp_nonce, ccmp_aad,
                           sizeof(ccmp_aad), packet, sizeof(packet),
                           ccmp_mic)) {
        fputs("WPA2 CCMP tamper test setup failed\n", stderr);
        return 1;
    }
    ccmp_mic[0] ^= 1u;
    if (wpa2_ccmp_decrypt(ccmp_key, ccmp_nonce, ccmp_aad,
                          sizeof(ccmp_aad), packet, sizeof(packet),
                          ccmp_mic)) {
        fputs("WPA2 CCMP accepted a tampered MIC\n", stderr);
        return 1;
    }
    for (uint32_t index = 0u; index < sizeof(packet); ++index) {
        if (packet[index] != 0u) {
            fputs("WPA2 CCMP exposed unauthenticated plaintext\n", stderr);
            return 1;
        }
    }

    if (wpa2_derive_pmk(password, 7u, ssid, sizeof(ssid) - 1u, pmk) ||
        !matches_hex(pmk,
                     "00000000000000000000000000000000"
                     "00000000000000000000000000000000",
                     sizeof(pmk)) ||
        wpa2_derive_pmk(password, sizeof(password) - 1u, ssid, 33u, pmk)) {
        fputs("WPA2 PBKDF2 input validation test failed\n", stderr);
        return 1;
    }
    return 0;
}
