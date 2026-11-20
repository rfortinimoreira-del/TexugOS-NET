#include <stddef.h>
#include <stdint.h>

#include "wpa2_crypto.h"

#define SHA1_BLOCK_SIZE 64u
#define SHA1_DIGEST_SIZE 20u
#define WPA2_PMK_SIZE 32u
#define WPA2_PTK_SIZE 64u
#define WPA2_PBKDF2_ITERATIONS 4096u
#define AES_BLOCK_SIZE 16u
#define AES_ROUND_KEY_SIZE 176u
#define CCMP_MIC_SIZE 8u
#define CCMP_NONCE_SIZE 13u

struct sha1_context {
    uint32_t state[5];
    uint64_t bit_count;
    uint8_t block[SHA1_BLOCK_SIZE];
    uint32_t block_length;
};

static const uint8_t aes_sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
    0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
    0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
    0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
    0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
    0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
    0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
    0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
    0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
    0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
    0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
    0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static uint32_t rotate_left(uint32_t value, uint32_t amount)
{
    return (value << amount) | (value >> (32u - amount));
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void write_be64(uint8_t *bytes, uint64_t value)
{
    for (uint32_t index = 0u; index < 8u; ++index) {
        bytes[index] = (uint8_t)(value >> (56u - index * 8u));
    }
}

static uint8_t aes_xtime(uint8_t value)
{
    return (uint8_t)((value << 1) ^ ((value & 0x80u) != 0u ? 0x1bu : 0u));
}

static void aes_expand_key(const uint8_t key[16],
                           uint8_t round_keys[AES_ROUND_KEY_SIZE])
{
    static const uint8_t round_constants[10] = {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
    };
    for (uint32_t index = 0u; index < 16u; ++index) {
        round_keys[index] = key[index];
    }

    uint32_t generated = 16u;
    uint32_t round = 0u;
    uint8_t temp[4];
    while (generated < AES_ROUND_KEY_SIZE) {
        for (uint32_t index = 0u; index < 4u; ++index) {
            temp[index] = round_keys[generated - 4u + index];
        }
        if ((generated & 15u) == 0u) {
            uint8_t first = temp[0];
            temp[0] = (uint8_t)(aes_sbox[temp[1]] ^ round_constants[round++]);
            temp[1] = aes_sbox[temp[2]];
            temp[2] = aes_sbox[temp[3]];
            temp[3] = aes_sbox[first];
        }
        for (uint32_t index = 0u; index < 4u; ++index) {
            round_keys[generated] =
                round_keys[generated - 16u] ^ temp[index];
            ++generated;
        }
    }
}

static void aes_add_round_key(uint8_t state[AES_BLOCK_SIZE],
                              const uint8_t *round_key)
{
    for (uint32_t index = 0u; index < AES_BLOCK_SIZE; ++index) {
        state[index] ^= round_key[index];
    }
}

static void aes_shift_rows(uint8_t state[AES_BLOCK_SIZE])
{
    uint8_t shifted[AES_BLOCK_SIZE];
    for (uint32_t row = 0u; row < 4u; ++row) {
        for (uint32_t column = 0u; column < 4u; ++column) {
            shifted[row + 4u * column] =
                state[row + 4u * ((column + row) & 3u)];
        }
    }
    for (uint32_t index = 0u; index < AES_BLOCK_SIZE; ++index) {
        state[index] = shifted[index];
    }
}

static void aes_mix_columns(uint8_t state[AES_BLOCK_SIZE])
{
    for (uint32_t column = 0u; column < 4u; ++column) {
        uint32_t offset = column * 4u;
        uint8_t a = state[offset];
        uint8_t b = state[offset + 1u];
        uint8_t c = state[offset + 2u];
        uint8_t d = state[offset + 3u];
        uint8_t all = a ^ b ^ c ^ d;
        state[offset] = a ^ all ^ aes_xtime((uint8_t)(a ^ b));
        state[offset + 1u] = b ^ all ^ aes_xtime((uint8_t)(b ^ c));
        state[offset + 2u] = c ^ all ^ aes_xtime((uint8_t)(c ^ d));
        state[offset + 3u] = d ^ all ^ aes_xtime((uint8_t)(d ^ a));
    }
}

static void aes_encrypt_block(const uint8_t round_keys[AES_ROUND_KEY_SIZE],
                              const uint8_t input[AES_BLOCK_SIZE],
                              uint8_t output[AES_BLOCK_SIZE])
{
    uint8_t state[AES_BLOCK_SIZE];
    for (uint32_t index = 0u; index < AES_BLOCK_SIZE; ++index) {
        state[index] = input[index];
    }
    aes_add_round_key(state, round_keys);
    for (uint32_t round = 1u; round < 10u; ++round) {
        for (uint32_t index = 0u; index < AES_BLOCK_SIZE; ++index) {
            state[index] = aes_sbox[state[index]];
        }
        aes_shift_rows(state);
        aes_mix_columns(state);
        aes_add_round_key(state, round_keys + round * AES_BLOCK_SIZE);
    }
    for (uint32_t index = 0u; index < AES_BLOCK_SIZE; ++index) {
        state[index] = aes_sbox[state[index]];
    }
    aes_shift_rows(state);
    aes_add_round_key(state, round_keys + 10u * AES_BLOCK_SIZE);
    for (uint32_t index = 0u; index < AES_BLOCK_SIZE; ++index) {
        output[index] = state[index];
    }
}

static uint8_t aes_multiply(uint8_t left, uint8_t right)
{
    uint8_t result = 0u;
    for (uint32_t bit = 0u; bit < 8u; ++bit) {
        if ((right & 1u) != 0u) {
            result ^= left;
        }
        left = aes_xtime(left);
        right >>= 1;
    }
    return result;
}

static void aes_inverse_shift_rows(uint8_t state[AES_BLOCK_SIZE])
{
    uint8_t shifted[AES_BLOCK_SIZE];
    for (uint32_t row = 0u; row < 4u; ++row) {
        for (uint32_t column = 0u; column < 4u; ++column) {
            shifted[row + 4u * column] =
                state[row + 4u * ((column + 4u - row) & 3u)];
        }
    }
    for (uint32_t index = 0u; index < AES_BLOCK_SIZE; ++index) {
        state[index] = shifted[index];
    }
}

static void aes_inverse_sub_bytes(uint8_t state[AES_BLOCK_SIZE])
{
    static uint8_t inverse_sbox[256];
    static uint8_t initialized;
    if (initialized == 0u) {
        for (uint32_t index = 0u; index < 256u; ++index) {
            inverse_sbox[aes_sbox[index]] = (uint8_t)index;
        }
        initialized = 1u;
    }
    for (uint32_t index = 0u; index < AES_BLOCK_SIZE; ++index) {
        state[index] = inverse_sbox[state[index]];
    }
}

static void aes_inverse_mix_columns(uint8_t state[AES_BLOCK_SIZE])
{
    for (uint32_t column = 0u; column < 4u; ++column) {
        uint32_t offset = column * 4u;
        uint8_t a = state[offset];
        uint8_t b = state[offset + 1u];
        uint8_t c = state[offset + 2u];
        uint8_t d = state[offset + 3u];
        state[offset] = aes_multiply(a, 14u) ^ aes_multiply(b, 11u) ^
                        aes_multiply(c, 13u) ^ aes_multiply(d, 9u);
        state[offset + 1u] = aes_multiply(a, 9u) ^ aes_multiply(b, 14u) ^
                             aes_multiply(c, 11u) ^ aes_multiply(d, 13u);
        state[offset + 2u] = aes_multiply(a, 13u) ^ aes_multiply(b, 9u) ^
                             aes_multiply(c, 14u) ^ aes_multiply(d, 11u);
        state[offset + 3u] = aes_multiply(a, 11u) ^ aes_multiply(b, 13u) ^
                             aes_multiply(c, 9u) ^ aes_multiply(d, 14u);
    }
}

static void aes_decrypt_block(const uint8_t round_keys[AES_ROUND_KEY_SIZE],
                              const uint8_t input[AES_BLOCK_SIZE],
                              uint8_t output[AES_BLOCK_SIZE])
{
    uint8_t state[AES_BLOCK_SIZE];
    for (uint32_t index = 0u; index < AES_BLOCK_SIZE; ++index) {
        state[index] = input[index];
    }
    aes_add_round_key(state, round_keys + 10u * AES_BLOCK_SIZE);
    for (uint32_t round = 9u; round != 0u; --round) {
        aes_inverse_shift_rows(state);
        aes_inverse_sub_bytes(state);
        aes_add_round_key(state, round_keys + round * AES_BLOCK_SIZE);
        aes_inverse_mix_columns(state);
    }
    aes_inverse_shift_rows(state);
    aes_inverse_sub_bytes(state);
    aes_add_round_key(state, round_keys);
    for (uint32_t index = 0u; index < AES_BLOCK_SIZE; ++index) {
        output[index] = state[index];
    }
}

int wpa2_aes_unwrap(const uint8_t kek[16], const uint8_t *wrapped,
                    uint32_t wrapped_length, uint8_t *plain,
                    uint32_t plain_capacity, uint32_t *plain_length)
{
    static const uint8_t initial_value[8] = {
        0xa6u, 0xa6u, 0xa6u, 0xa6u, 0xa6u, 0xa6u, 0xa6u, 0xa6u,
    };
    if (kek == 0 || wrapped == 0 || plain == 0 || plain_length == 0 ||
        wrapped_length < 24u || (wrapped_length & 7u) != 0u ||
        plain_capacity < wrapped_length - 8u) {
        return 0;
    }

    uint32_t block_count = wrapped_length / 8u - 1u;
    uint8_t a[8];
    uint8_t blocks[512];
    if (wrapped_length > sizeof(blocks) + 8u) {
        return 0;
    }
    for (uint32_t index = 0u; index < 8u; ++index) {
        a[index] = wrapped[index];
    }
    for (uint32_t index = 0u; index < block_count * 8u; ++index) {
        blocks[index] = wrapped[index + 8u];
    }

    uint8_t round_keys[AES_ROUND_KEY_SIZE];
    aes_expand_key(kek, round_keys);
    uint64_t counter = (uint64_t)block_count * 6u;
    uint8_t input[AES_BLOCK_SIZE];
    uint8_t output[AES_BLOCK_SIZE];
    for (uint32_t round = 0u; round < 6u; ++round) {
        for (uint32_t reverse = block_count; reverse != 0u; --reverse) {
            uint64_t value = counter--;
            for (uint32_t index = 0u; index < 8u; ++index) {
                input[index] = a[index];
            }
            for (uint32_t index = 0u; index < 8u; ++index) {
                input[7u - index] ^= (uint8_t)(value >> (index * 8u));
            }
            for (uint32_t index = 0u; index < 8u; ++index) {
                input[index + 8u] = blocks[(reverse - 1u) * 8u + index];
            }
            aes_decrypt_block(round_keys, input, output);
            for (uint32_t index = 0u; index < 8u; ++index) {
                a[index] = output[index];
                blocks[(reverse - 1u) * 8u + index] = output[index + 8u];
            }
        }
    }

    uint8_t difference = 0u;
    for (uint32_t index = 0u; index < sizeof(initial_value); ++index) {
        difference |= a[index] ^ initial_value[index];
    }
    if (difference != 0u) {
        for (uint32_t index = 0u; index < wrapped_length - 8u; ++index) {
            plain[index] = 0u;
        }
        *plain_length = 0u;
        return 0;
    }
    for (uint32_t index = 0u; index < block_count * 8u; ++index) {
        plain[index] = blocks[index];
    }
    *plain_length = block_count * 8u;
    return 1;
}

int wpa2_aes_wrap(const uint8_t kek[16], const uint8_t *plain,
                  uint32_t plain_length, uint8_t *wrapped,
                  uint32_t wrapped_capacity, uint32_t *wrapped_length)
{
    static const uint8_t initial_value[8] = {
        0xa6u, 0xa6u, 0xa6u, 0xa6u, 0xa6u, 0xa6u, 0xa6u, 0xa6u,
    };
    if (kek == 0 || plain == 0 || wrapped == 0 || wrapped_length == 0 ||
        plain_length < 16u || (plain_length & 7u) != 0u ||
        plain_length > 512u || wrapped_capacity < plain_length + 8u) {
        return 0;
    }

    uint32_t block_count = plain_length / 8u;
    uint8_t a[8];
    uint8_t blocks[512];
    for (uint32_t index = 0u; index < sizeof(a); ++index) {
        a[index] = initial_value[index];
    }
    for (uint32_t index = 0u; index < plain_length; ++index) {
        blocks[index] = plain[index];
    }
    uint8_t round_keys[AES_ROUND_KEY_SIZE];
    aes_expand_key(kek, round_keys);
    uint8_t input[AES_BLOCK_SIZE];
    uint8_t output[AES_BLOCK_SIZE];
    for (uint32_t round = 0u; round < 6u; ++round) {
        for (uint32_t block = 1u; block <= block_count; ++block) {
            for (uint32_t index = 0u; index < sizeof(a); ++index) {
                input[index] = a[index];
            }
            for (uint32_t index = 0u; index < 8u; ++index) {
                input[index + 8u] = blocks[(block - 1u) * 8u + index];
            }
            aes_encrypt_block(round_keys, input, output);
            uint64_t value = (uint64_t)block_count * round + block;
            for (uint32_t index = 0u; index < sizeof(a); ++index) {
                a[index] = output[index] ^
                           (uint8_t)(value >> (56u - index * 8u));
                blocks[(block - 1u) * 8u + index] = output[index + 8u];
            }
        }
    }
    for (uint32_t index = 0u; index < sizeof(a); ++index) {
        wrapped[index] = a[index];
    }
    for (uint32_t index = 0u; index < plain_length; ++index) {
        wrapped[index + 8u] = blocks[index];
    }
    *wrapped_length = plain_length + 8u;
    return 1;
}

static void ccm_mac_block(const uint8_t round_keys[AES_ROUND_KEY_SIZE],
                          uint8_t mac[AES_BLOCK_SIZE],
                          const uint8_t block[AES_BLOCK_SIZE])
{
    uint8_t input[AES_BLOCK_SIZE];
    for (uint32_t index = 0u; index < AES_BLOCK_SIZE; ++index) {
        input[index] = mac[index] ^ block[index];
    }
    aes_encrypt_block(round_keys, input, mac);
}

static void ccm_mac_data(const uint8_t round_keys[AES_ROUND_KEY_SIZE],
                         uint8_t mac[AES_BLOCK_SIZE], const uint8_t *data,
                         uint32_t length)
{
    uint8_t block[AES_BLOCK_SIZE];
    while (length != 0u) {
        uint32_t chunk = length < AES_BLOCK_SIZE ? length : AES_BLOCK_SIZE;
        for (uint32_t index = 0u; index < AES_BLOCK_SIZE; ++index) {
            block[index] = index < chunk ? data[index] : 0u;
        }
        ccm_mac_block(round_keys, mac, block);
        data += chunk;
        length -= chunk;
    }
}

static void ccm_build_counter(const uint8_t nonce[CCMP_NONCE_SIZE],
                              uint16_t counter, uint8_t block[AES_BLOCK_SIZE])
{
    block[0] = 1u;
    for (uint32_t index = 0u; index < CCMP_NONCE_SIZE; ++index) {
        block[index + 1u] = nonce[index];
    }
    block[14] = (uint8_t)(counter >> 8);
    block[15] = (uint8_t)counter;
}

static int ccmp_crypt(const uint8_t temporal_key[16],
                      const uint8_t nonce[CCMP_NONCE_SIZE],
                      const uint8_t *aad, uint32_t aad_length, uint8_t *data,
                      uint32_t data_length, uint8_t mic[CCMP_MIC_SIZE],
                      int decrypt)
{
    if (temporal_key == 0 || nonce == 0 ||
        (aad == 0 && aad_length != 0u) ||
        (data == 0 && data_length != 0u) || mic == 0 ||
        aad_length > 0xfeffu || data_length > 0xffffu) {
        return 0;
    }

    uint8_t round_keys[AES_ROUND_KEY_SIZE];
    aes_expand_key(temporal_key, round_keys);
    uint8_t mac[AES_BLOCK_SIZE] = {0};
    uint8_t block[AES_BLOCK_SIZE] = {0};
    block[0] = (uint8_t)((aad_length != 0u ? 0x40u : 0u) | 0x19u);
    for (uint32_t index = 0u; index < CCMP_NONCE_SIZE; ++index) {
        block[index + 1u] = nonce[index];
    }
    block[14] = (uint8_t)(data_length >> 8);
    block[15] = (uint8_t)data_length;
    ccm_mac_block(round_keys, mac, block);

    if (aad_length != 0u) {
        block[0] = (uint8_t)(aad_length >> 8);
        block[1] = (uint8_t)aad_length;
        uint32_t first_chunk = aad_length < 14u ? aad_length : 14u;
        for (uint32_t index = 0u; index < 14u; ++index) {
            block[index + 2u] =
                index < first_chunk ? aad[index] : 0u;
        }
        ccm_mac_block(round_keys, mac, block);
        ccm_mac_data(round_keys, mac, aad + first_chunk,
                     aad_length - first_chunk);
    }

    if (decrypt) {
        uint32_t offset = 0u;
        uint16_t counter = 1u;
        while (offset < data_length) {
            uint8_t stream[AES_BLOCK_SIZE];
            ccm_build_counter(nonce, counter++, block);
            aes_encrypt_block(round_keys, block, stream);
            uint32_t chunk = data_length - offset;
            if (chunk > AES_BLOCK_SIZE) {
                chunk = AES_BLOCK_SIZE;
            }
            for (uint32_t index = 0u; index < chunk; ++index) {
                data[offset + index] ^= stream[index];
            }
            offset += chunk;
        }
    }

    ccm_mac_data(round_keys, mac, data, data_length);
    uint8_t counter_zero[AES_BLOCK_SIZE];
    uint8_t stream_zero[AES_BLOCK_SIZE];
    ccm_build_counter(nonce, 0u, counter_zero);
    aes_encrypt_block(round_keys, counter_zero, stream_zero);
    uint8_t expected_mic[CCMP_MIC_SIZE];
    for (uint32_t index = 0u; index < CCMP_MIC_SIZE; ++index) {
        expected_mic[index] = mac[index] ^ stream_zero[index];
    }

    if (decrypt) {
        uint8_t difference = 0u;
        for (uint32_t index = 0u; index < CCMP_MIC_SIZE; ++index) {
            difference |= expected_mic[index] ^ mic[index];
        }
        if (difference != 0u) {
            for (uint32_t index = 0u; index < data_length; ++index) {
                data[index] = 0u;
            }
            return 0;
        }
        return 1;
    }

    for (uint32_t index = 0u; index < CCMP_MIC_SIZE; ++index) {
        mic[index] = expected_mic[index];
    }
    uint32_t offset = 0u;
    uint16_t counter = 1u;
    while (offset < data_length) {
        uint8_t stream[AES_BLOCK_SIZE];
        ccm_build_counter(nonce, counter++, block);
        aes_encrypt_block(round_keys, block, stream);
        uint32_t chunk = data_length - offset;
        if (chunk > AES_BLOCK_SIZE) {
            chunk = AES_BLOCK_SIZE;
        }
        for (uint32_t index = 0u; index < chunk; ++index) {
            data[offset + index] ^= stream[index];
        }
        offset += chunk;
    }
    return 1;
}

static void sha1_transform(struct sha1_context *context,
                           const uint8_t block[SHA1_BLOCK_SIZE])
{
    uint32_t words[80];
    for (uint32_t index = 0u; index < 16u; ++index) {
        words[index] = read_be32(block + index * 4u);
    }
    for (uint32_t index = 16u; index < 80u; ++index) {
        words[index] = rotate_left(
            words[index - 3u] ^ words[index - 8u] ^
            words[index - 14u] ^ words[index - 16u], 1u);
    }

    uint32_t a = context->state[0];
    uint32_t b = context->state[1];
    uint32_t c = context->state[2];
    uint32_t d = context->state[3];
    uint32_t e = context->state[4];
    for (uint32_t index = 0u; index < 80u; ++index) {
        uint32_t function;
        uint32_t constant;
        if (index < 20u) {
            function = (b & c) | (~b & d);
            constant = 0x5a827999u;
        } else if (index < 40u) {
            function = b ^ c ^ d;
            constant = 0x6ed9eba1u;
        } else if (index < 60u) {
            function = (b & c) | (b & d) | (c & d);
            constant = 0x8f1bbcdcu;
        } else {
            function = b ^ c ^ d;
            constant = 0xca62c1d6u;
        }
        uint32_t temp = rotate_left(a, 5u) + function + e + constant +
                        words[index];
        e = d;
        d = c;
        c = rotate_left(b, 30u);
        b = a;
        a = temp;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
}

static void sha1_initialize(struct sha1_context *context)
{
    context->state[0] = 0x67452301u;
    context->state[1] = 0xefcdab89u;
    context->state[2] = 0x98badcfeu;
    context->state[3] = 0x10325476u;
    context->state[4] = 0xc3d2e1f0u;
    context->bit_count = 0u;
    context->block_length = 0u;
}

static void sha1_update(struct sha1_context *context, const uint8_t *data,
                        uint32_t length)
{
    context->bit_count += (uint64_t)length * 8u;
    while (length != 0u) {
        uint32_t available = SHA1_BLOCK_SIZE - context->block_length;
        uint32_t copy_length = length < available ? length : available;
        for (uint32_t index = 0u; index < copy_length; ++index) {
            context->block[context->block_length + index] = data[index];
        }
        context->block_length += copy_length;
        data += copy_length;
        length -= copy_length;
        if (context->block_length == SHA1_BLOCK_SIZE) {
            sha1_transform(context, context->block);
            context->block_length = 0u;
        }
    }
}

static void sha1_finalize(struct sha1_context *context,
                          uint8_t digest[SHA1_DIGEST_SIZE])
{
    uint64_t original_bit_count = context->bit_count;
    uint8_t marker = 0x80u;
    sha1_update(context, &marker, 1u);
    uint8_t zero = 0u;
    while (context->block_length != 56u) {
        sha1_update(context, &zero, 1u);
    }

    uint8_t length_bytes[8];
    write_be64(length_bytes, original_bit_count);
    sha1_update(context, length_bytes, sizeof(length_bytes));
    for (uint32_t index = 0u; index < 5u; ++index) {
        write_be32(digest + index * 4u, context->state[index]);
    }
}

static void hmac_sha1(const uint8_t *key, uint32_t key_length,
                      const uint8_t *data, uint32_t data_length,
                      uint8_t digest[SHA1_DIGEST_SIZE])
{
    uint8_t key_block[SHA1_BLOCK_SIZE] = {0};
    if (key_length > SHA1_BLOCK_SIZE) {
        struct sha1_context hash;
        sha1_initialize(&hash);
        sha1_update(&hash, key, key_length);
        sha1_finalize(&hash, key_block);
    } else {
        for (uint32_t index = 0u; index < key_length; ++index) {
            key_block[index] = key[index];
        }
    }

    uint8_t inner_pad[SHA1_BLOCK_SIZE];
    uint8_t outer_pad[SHA1_BLOCK_SIZE];
    for (uint32_t index = 0u; index < SHA1_BLOCK_SIZE; ++index) {
        inner_pad[index] = key_block[index] ^ 0x36u;
        outer_pad[index] = key_block[index] ^ 0x5cu;
    }

    uint8_t inner_digest[SHA1_DIGEST_SIZE];
    struct sha1_context hash;
    sha1_initialize(&hash);
    sha1_update(&hash, inner_pad, sizeof(inner_pad));
    sha1_update(&hash, data, data_length);
    sha1_finalize(&hash, inner_digest);

    sha1_initialize(&hash);
    sha1_update(&hash, outer_pad, sizeof(outer_pad));
    sha1_update(&hash, inner_digest, sizeof(inner_digest));
    sha1_finalize(&hash, digest);
}

int wpa2_derive_pmk(const uint8_t *passphrase, uint32_t passphrase_length,
                    const uint8_t *ssid, uint32_t ssid_length,
                    uint8_t pmk[WPA2_PMK_SIZE])
{
    if (pmk == 0) {
        return 0;
    }
    for (uint32_t index = 0u; index < WPA2_PMK_SIZE; ++index) {
        pmk[index] = 0u;
    }
    if (passphrase == 0 || ssid == 0 || passphrase_length < 8u ||
        passphrase_length > 63u || ssid_length == 0u || ssid_length > 32u) {
        return 0;
    }

    uint8_t salt[36];
    for (uint32_t index = 0u; index < ssid_length; ++index) {
        salt[index] = ssid[index];
    }

    for (uint32_t block = 1u; block <= 2u; ++block) {
        write_be32(salt + ssid_length, block);
        uint8_t u[SHA1_DIGEST_SIZE];
        uint8_t result[SHA1_DIGEST_SIZE];
        hmac_sha1(passphrase, passphrase_length, salt, ssid_length + 4u, u);
        for (uint32_t index = 0u; index < SHA1_DIGEST_SIZE; ++index) {
            result[index] = u[index];
        }

        for (uint32_t iteration = 1u;
             iteration < WPA2_PBKDF2_ITERATIONS; ++iteration) {
            hmac_sha1(passphrase, passphrase_length, u, sizeof(u), u);
            for (uint32_t index = 0u; index < SHA1_DIGEST_SIZE; ++index) {
                result[index] ^= u[index];
            }
        }

        uint32_t output_offset = (block - 1u) * SHA1_DIGEST_SIZE;
        uint32_t output_length = WPA2_PMK_SIZE - output_offset;
        if (output_length > SHA1_DIGEST_SIZE) {
            output_length = SHA1_DIGEST_SIZE;
        }
        for (uint32_t index = 0u; index < output_length; ++index) {
            pmk[output_offset + index] = result[index];
        }
    }
    return 1;
}

static int compare_bytes(const uint8_t *left, const uint8_t *right,
                         uint32_t length)
{
    for (uint32_t index = 0u; index < length; ++index) {
        if (left[index] != right[index]) {
            return left[index] < right[index] ? -1 : 1;
        }
    }
    return 0;
}

int wpa2_derive_ptk(const uint8_t pmk[WPA2_PMK_SIZE],
                    const uint8_t authenticator_address[6],
                    const uint8_t supplicant_address[6],
                    const uint8_t authenticator_nonce[32],
                    const uint8_t supplicant_nonce[32],
                    uint8_t ptk[WPA2_PTK_SIZE])
{
    if (pmk == 0 || authenticator_address == 0 ||
        supplicant_address == 0 || authenticator_nonce == 0 ||
        supplicant_nonce == 0 || ptk == 0) {
        return 0;
    }

    static const uint8_t label[] = "Pairwise key expansion";
    uint8_t input[100];
    uint32_t input_length = sizeof(label);
    for (uint32_t index = 0u; index < sizeof(label); ++index) {
        input[index] = label[index];
    }

    const uint8_t *first_address = authenticator_address;
    const uint8_t *second_address = supplicant_address;
    if (compare_bytes(first_address, second_address, 6u) > 0) {
        first_address = supplicant_address;
        second_address = authenticator_address;
    }
    uint32_t offset = input_length;
    for (uint32_t index = 0u; index < 6u; ++index) {
        input[offset++] = first_address[index];
    }
    for (uint32_t index = 0u; index < 6u; ++index) {
        input[offset++] = second_address[index];
    }

    const uint8_t *first_nonce = authenticator_nonce;
    const uint8_t *second_nonce = supplicant_nonce;
    if (compare_bytes(first_nonce, second_nonce, 32u) > 0) {
        first_nonce = supplicant_nonce;
        second_nonce = authenticator_nonce;
    }
    for (uint32_t index = 0u; index < 32u; ++index) {
        input[offset++] = first_nonce[index];
    }
    for (uint32_t index = 0u; index < 32u; ++index) {
        input[offset++] = second_nonce[index];
    }

    uint8_t digest[SHA1_DIGEST_SIZE];
    uint8_t counter = 0u;
    uint32_t generated = 0u;
    while (generated < WPA2_PTK_SIZE) {
        input[offset] = counter++;
        hmac_sha1(pmk, WPA2_PMK_SIZE, input, offset + 1u, digest);
        for (uint32_t index = 0u;
             index < SHA1_DIGEST_SIZE && generated < WPA2_PTK_SIZE;
             ++index) {
            ptk[generated++] = digest[index];
        }
    }
    return 1;
}

int wpa2_derive_snonce(const uint8_t pmk[WPA2_PMK_SIZE],
                       const uint8_t authenticator_address[6],
                       const uint8_t supplicant_address[6],
                       const uint8_t authenticator_nonce[32],
                       const uint8_t replay_counter[8],
                       uint32_t ptk_generation, uint8_t snonce[32])
{
    static const uint8_t label[] = "TexugOS WPA2 SNonce";
    if (pmk == 0 || authenticator_address == 0 ||
        supplicant_address == 0 || authenticator_nonce == 0 ||
        replay_counter == 0 || snonce == 0) {
        return 0;
    }

    uint8_t input[sizeof(label) + 6u + 6u + 32u + 8u + 4u + 1u];
    uint32_t offset = 0u;
    for (uint32_t index = 0u; index < sizeof(label); ++index) {
        input[offset++] = label[index];
    }
    for (uint32_t index = 0u; index < 6u; ++index) {
        input[offset++] = authenticator_address[index];
    }
    for (uint32_t index = 0u; index < 6u; ++index) {
        input[offset++] = supplicant_address[index];
    }
    for (uint32_t index = 0u; index < 32u; ++index) {
        input[offset++] = authenticator_nonce[index];
    }
    for (uint32_t index = 0u; index < 8u; ++index) {
        input[offset++] = replay_counter[index];
    }
    write_be32(input + offset, ptk_generation);
    offset += 4u;

    uint8_t digest[SHA1_DIGEST_SIZE];
    uint32_t generated = 0u;
    for (uint8_t block = 0u; generated < 32u; ++block) {
        input[offset] = block;
        hmac_sha1(pmk, WPA2_PMK_SIZE, input, offset + 1u, digest);
        for (uint32_t index = 0u;
             index < sizeof(digest) && generated < 32u; ++index) {
            snonce[generated++] = digest[index];
        }
    }
    volatile uint8_t *wipe = digest;
    for (uint32_t index = 0u; index < sizeof(digest); ++index) {
        wipe[index] = 0u;
    }
    wipe = input;
    for (uint32_t index = 0u; index < sizeof(input); ++index) {
        wipe[index] = 0u;
    }
    return 1;
}

int wpa2_eapol_mic(const uint8_t kck[16], const uint8_t *eapol,
                   uint32_t eapol_length, uint8_t mic[16])
{
    if (kck == 0 || (eapol == 0 && eapol_length != 0u) || mic == 0) {
        return 0;
    }
    uint8_t digest[SHA1_DIGEST_SIZE];
    hmac_sha1(kck, 16u, eapol, eapol_length, digest);
    for (uint32_t index = 0u; index < 16u; ++index) {
        mic[index] = digest[index];
    }
    return 1;
}

int wpa2_ccmp_encrypt(const uint8_t temporal_key[16],
                      const uint8_t nonce[CCMP_NONCE_SIZE], const uint8_t *aad,
                      uint32_t aad_length, uint8_t *data,
                      uint32_t data_length, uint8_t mic[CCMP_MIC_SIZE])
{
    return ccmp_crypt(temporal_key, nonce, aad, aad_length, data, data_length,
                      mic, 0);
}

int wpa2_ccmp_decrypt(const uint8_t temporal_key[16],
                      const uint8_t nonce[CCMP_NONCE_SIZE], const uint8_t *aad,
                      uint32_t aad_length, uint8_t *data,
                      uint32_t data_length, const uint8_t mic[CCMP_MIC_SIZE])
{
    uint8_t checked_mic[CCMP_MIC_SIZE];
    if (mic == 0) {
        return 0;
    }
    for (uint32_t index = 0u; index < CCMP_MIC_SIZE; ++index) {
        checked_mic[index] = mic[index];
    }
    return ccmp_crypt(temporal_key, nonce, aad, aad_length, data, data_length,
                      checked_mic, 1);
}
