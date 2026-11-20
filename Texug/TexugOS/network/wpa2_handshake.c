#include <stdint.h>

#include "wpa2_crypto.h"
#include "wpa2_handshake.h"

#define EAPOL_HEADER_SIZE 4u
#define EAPOL_KEY_BODY_SIZE 95u
#define EAPOL_KEY_MIC_OFFSET 81u
#define EAPOL_KEY_DATA_LENGTH_OFFSET 97u
#define EAPOL_KEY_DATA_OFFSET 99u
#define EAPOL_KEY_DESCRIPTOR_TYPE_RSN 2u
#define EAPOL_KEY_INFO_VERSION_MASK 0x0007u
#define EAPOL_KEY_INFO_PAIRWISE 0x0008u
#define EAPOL_KEY_INFO_INSTALL 0x0040u
#define EAPOL_KEY_INFO_ACK 0x0080u
#define EAPOL_KEY_INFO_MIC 0x0100u
#define EAPOL_KEY_INFO_SECURE 0x0200u
#define EAPOL_KEY_INFO_ENCRYPTED_DATA 0x1000u
#define EAPOL_KEY_LENGTH_CCMP 16u
#define WPA2_KEY_INFO_VERSION 2u
#define WPA2_GTK_KDE_ID 221u
#define WPA2_GTK_KDE_TYPE 1u
#define WPA2_GTK_SIZE 16u

static void clear_bytes(void *buffer, uint32_t length)
{
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;
    for (uint32_t index = 0u; index < length; ++index) {
        bytes[index] = 0u;
    }
}

static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint64_t read_be64(const uint8_t *bytes)
{
    uint64_t value = 0u;
    for (uint32_t index = 0u; index < 8u; ++index) {
        value = (value << 8) | bytes[index];
    }
    return value;
}

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static int bytes_equal(const uint8_t *left, const uint8_t *right,
                       uint32_t length)
{
    uint8_t difference = 0u;
    for (uint32_t index = 0u; index < length; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0u;
}

static int valid_unicast_address(const uint8_t address[6])
{
    uint8_t any_nonzero = 0u;
    uint8_t all_ones = 0xffu;
    for (uint32_t index = 0u; index < 6u; ++index) {
        any_nonzero |= address[index];
        all_ones &= address[index];
    }
    return any_nonzero != 0u && all_ones != 0xffu &&
           (address[0] & 1u) == 0u;
}

static int replay_counter_equal(const uint8_t left[8],
                                const uint8_t right[8])
{
    return bytes_equal(left, right, 8u);
}

static int generate_snonce(struct wpa2_handshake *handshake,
                           const uint8_t replay_counter[8])
{
    if (handshake->random_fill != 0 &&
        handshake->random_fill(handshake->random_context,
                               handshake->supplicant_nonce,
                               sizeof(handshake->supplicant_nonce))) {
        return 1;
    }
    return wpa2_derive_snonce(
        handshake->pmk, handshake->authenticator_address,
        handshake->supplicant_address, handshake->authenticator_nonce,
        replay_counter, handshake->ptk_generation,
        handshake->supplicant_nonce);
}

static int verify_eapol_mic(const uint8_t ptk[64],
                            const uint8_t *eapol, uint32_t eapol_length)
{
    uint8_t packet[WPA2_EAPOL_MAX_SIZE];
    uint8_t expected[16];
    if (eapol_length > sizeof(packet) || eapol_length < EAPOL_KEY_DATA_OFFSET) {
        return 0;
    }
    for (uint32_t index = 0u; index < eapol_length; ++index) {
        packet[index] = eapol[index];
    }
    for (uint32_t index = 0u; index < sizeof(expected); ++index) {
        packet[EAPOL_KEY_MIC_OFFSET + index] = 0u;
    }
    if (!wpa2_eapol_mic(ptk, packet, eapol_length, expected)) {
        clear_bytes(packet, eapol_length);
        return 0;
    }
    int valid = bytes_equal(expected, eapol + EAPOL_KEY_MIC_OFFSET,
                            sizeof(expected));
    clear_bytes(packet, eapol_length);
    clear_bytes(expected, sizeof(expected));
    return valid;
}

static int build_key_response(const uint8_t ptk[64],
                              uint16_t key_info, const uint8_t replay[8],
                              const uint8_t nonce[32],
                              const uint8_t *key_data,
                              uint16_t key_data_length, uint8_t *output,
                              uint32_t capacity, uint32_t *output_length)
{
    static const uint8_t rsn_ie[22] = {
        48u, 20u, 1u, 0u, 0u, 0x0fu, 0xacu, 4u,
        1u, 0u, 0u, 0x0fu, 0xacu, 4u, 1u, 0u,
        0u, 0x0fu, 0xacu, 2u, 0u, 0u,
    };
    if (key_data == 0 && key_data_length == 0u &&
        (key_info & EAPOL_KEY_INFO_MIC) != 0u &&
        (key_info & EAPOL_KEY_INFO_SECURE) == 0u) {
        key_data = rsn_ie;
        key_data_length = sizeof(rsn_ie);
    }

    uint32_t total_length = EAPOL_KEY_DATA_OFFSET + key_data_length;
    if (output == 0 || output_length == 0 || total_length > capacity ||
        total_length > WPA2_EAPOL_MAX_SIZE ||
        (key_data == 0 && key_data_length != 0u)) {
        return 0;
    }
    for (uint32_t index = 0u; index < total_length; ++index) {
        output[index] = 0u;
    }
    output[0] = 2u;
    output[1] = 3u;
    write_be16(output + 2u, (uint16_t)(EAPOL_KEY_BODY_SIZE +
                                       key_data_length));
    output[4] = EAPOL_KEY_DESCRIPTOR_TYPE_RSN;
    write_be16(output + 5u, key_info);
    write_be16(output + 7u, EAPOL_KEY_LENGTH_CCMP);
    for (uint32_t index = 0u; index < 8u; ++index) {
        output[9u + index] = replay[index];
    }
    for (uint32_t index = 0u; index < 32u; ++index) {
        output[17u + index] = nonce == 0 ? 0u : nonce[index];
    }
    write_be16(output + EAPOL_KEY_DATA_LENGTH_OFFSET, key_data_length);
    for (uint32_t index = 0u; index < key_data_length; ++index) {
        output[EAPOL_KEY_DATA_OFFSET + index] = key_data[index];
    }

    uint8_t mic[16];
    if (!wpa2_eapol_mic(ptk, output, total_length, mic)) {
        clear_bytes(output, total_length);
        return 0;
    }
    for (uint32_t index = 0u; index < sizeof(mic); ++index) {
        output[EAPOL_KEY_MIC_OFFSET + index] = mic[index];
    }
    clear_bytes(mic, sizeof(mic));
    *output_length = total_length;
    return 1;
}

static int parse_gtk_kde(const uint8_t *key_data, uint32_t key_data_length,
                         uint8_t gtk[WPA2_GTK_SIZE], uint8_t *gtk_id)
{
    uint32_t offset = 0u;
    uint8_t found = 0u;
    while (offset < key_data_length) {
        if (key_data_length - offset < 2u) {
            return 0;
        }
        uint8_t element_id = key_data[offset];
        uint8_t element_length = key_data[offset + 1u];
        offset += 2u;
        if (element_length > key_data_length - offset) {
            return 0;
        }
        const uint8_t *element = key_data + offset;
        if (element_id == WPA2_GTK_KDE_ID && element_length >= 6u &&
            element[0] == 0u && element[1] == 0x0fu &&
            element[2] == 0xacu && element[3] == WPA2_GTK_KDE_TYPE) {
            if (found != 0u || element_length != 22u ||
                (element[4] & 0xf8u) != 0u) {
                return 0;
            }
            *gtk_id = element[4] & 0x03u;
            for (uint32_t index = 0u; index < WPA2_GTK_SIZE; ++index) {
                gtk[index] = element[6u + index];
            }
            found = 1u;
        }
        offset += element_length;
    }
    return found != 0u;
}

static enum wpa2_handshake_result process_group_key_message(
    struct wpa2_handshake *handshake, const uint8_t *eapol,
    uint32_t eapol_length, uint16_t key_data_length,
    uint8_t *response, uint32_t response_capacity,
    uint32_t *response_length)
{
    static const uint8_t zero_nonce[32] = {0};
    const uint16_t expected_info = WPA2_KEY_INFO_VERSION |
                                   EAPOL_KEY_INFO_ACK |
                                   EAPOL_KEY_INFO_MIC |
                                   EAPOL_KEY_INFO_SECURE |
                                   EAPOL_KEY_INFO_ENCRYPTED_DATA;
    const uint8_t *replay = eapol + 9u;
    if (handshake->state != WPA2_HANDSHAKE_ESTABLISHED ||
        read_be16(eapol + 5u) != expected_info ||
        key_data_length == 0u ||
        !bytes_equal(eapol + 17u, zero_nonce, sizeof(zero_nonce)) ||
        !verify_eapol_mic(handshake->ptk, eapol, eapol_length)) {
        return WPA2_HANDSHAKE_ERROR;
    }
    if (replay_counter_equal(replay, handshake->last_group_replay_counter) &&
        handshake->last_group_m2_length != 0u) {
        if (response_capacity < handshake->last_group_m2_length) {
            return WPA2_HANDSHAKE_ERROR;
        }
        for (uint32_t index = 0u; index < handshake->last_group_m2_length;
             ++index) {
            response[index] = handshake->last_group_m2[index];
        }
        *response_length = handshake->last_group_m2_length;
        return WPA2_HANDSHAKE_RESPONSE;
    }
    if (read_be64(replay) <=
            read_be64(handshake->last_group_replay_counter) ||
        handshake->gtk_generation == UINT32_MAX) {
        return WPA2_HANDSHAKE_ERROR;
    }

    uint8_t key_data[WPA2_EAPOL_MAX_SIZE];
    uint8_t gtk_candidate[WPA2_GTK_SIZE];
    uint8_t gtk_id_candidate = 0u;
    uint32_t plain_length = 0u;
    if (!wpa2_aes_unwrap(handshake->ptk + 16u,
                         eapol + EAPOL_KEY_DATA_OFFSET, key_data_length,
                         key_data, sizeof(key_data), &plain_length) ||
        !parse_gtk_kde(key_data, plain_length, gtk_candidate,
                       &gtk_id_candidate) ||
        !build_key_response(handshake->ptk,
                            WPA2_KEY_INFO_VERSION | EAPOL_KEY_INFO_MIC |
                                EAPOL_KEY_INFO_SECURE,
                            replay, zero_nonce, 0, 0u, response,
                            response_capacity, response_length)) {
        clear_bytes(key_data, sizeof(key_data));
        clear_bytes(gtk_candidate, sizeof(gtk_candidate));
        return WPA2_HANDSHAKE_ERROR;
    }
    clear_bytes(key_data, sizeof(key_data));
    for (uint32_t index = 0u; index < WPA2_GTK_SIZE; ++index) {
        handshake->gtk[index] = gtk_candidate[index];
    }
    clear_bytes(gtk_candidate, sizeof(gtk_candidate));
    handshake->gtk_id = gtk_id_candidate;
    handshake->gtk_valid = 1u;
    ++handshake->gtk_generation;
    for (uint32_t index = 0u; index < 8u; ++index) {
        handshake->last_group_replay_counter[index] = replay[index];
    }
    for (uint32_t index = 0u; index < *response_length; ++index) {
        handshake->last_group_m2[index] = response[index];
    }
    handshake->last_group_m2_length = (uint16_t)*response_length;
    return WPA2_HANDSHAKE_RESPONSE;
}

void wpa2_handshake_clear(struct wpa2_handshake *handshake)
{
    if (handshake != 0) {
        clear_bytes(handshake, sizeof(*handshake));
        handshake->state = WPA2_HANDSHAKE_IDLE;
    }
}

int wpa2_handshake_begin(struct wpa2_handshake *handshake,
                         const uint8_t *passphrase,
                         uint32_t passphrase_length, const uint8_t *ssid,
                         uint32_t ssid_length,
                         const uint8_t authenticator_address[6],
                         const uint8_t supplicant_address[6],
                         wpa2_random_fill_fn random_fill,
                         void *random_context)
{
    if (handshake == 0 || random_fill == 0 ||
        !valid_unicast_address(authenticator_address) ||
        !valid_unicast_address(supplicant_address)) {
        return 0;
    }
    wpa2_handshake_clear(handshake);
    if (!wpa2_derive_pmk(passphrase, passphrase_length, ssid, ssid_length,
                         handshake->pmk)) {
        wpa2_handshake_clear(handshake);
        return 0;
    }
    for (uint32_t index = 0u; index < 6u; ++index) {
        handshake->authenticator_address[index] = authenticator_address[index];
        handshake->supplicant_address[index] = supplicant_address[index];
    }
    handshake->state = WPA2_HANDSHAKE_WAIT_M1;
    handshake->random_fill = random_fill;
    handshake->random_context = random_context;
    return 1;
}

enum wpa2_handshake_result wpa2_handshake_process(
    struct wpa2_handshake *handshake, const uint8_t *eapol,
    uint32_t eapol_length, uint8_t *response, uint32_t response_capacity,
    uint32_t *response_length)
{
    if (response_length != 0) {
        *response_length = 0u;
    }
    if (handshake == 0 || eapol == 0 || response == 0 ||
        response_length == 0 || eapol_length < EAPOL_KEY_DATA_OFFSET ||
        eapol_length > WPA2_EAPOL_MAX_SIZE || eapol[0] < 1u ||
        eapol[0] > 2u || eapol[1] != 3u ||
        read_be16(eapol + 2u) != eapol_length - EAPOL_HEADER_SIZE ||
        eapol[4] != EAPOL_KEY_DESCRIPTOR_TYPE_RSN ||
        read_be16(eapol + 7u) != EAPOL_KEY_LENGTH_CCMP) {
        return WPA2_HANDSHAKE_IGNORE;
    }
    uint16_t descriptor_length = read_be16(eapol + 2u);
    if (descriptor_length < EAPOL_KEY_BODY_SIZE) {
        return WPA2_HANDSHAKE_IGNORE;
    }
    uint16_t key_data_length = read_be16(eapol + EAPOL_KEY_DATA_LENGTH_OFFSET);
    if ((uint32_t)key_data_length !=
        eapol_length - EAPOL_KEY_DATA_OFFSET) {
        return WPA2_HANDSHAKE_IGNORE;
    }

    uint16_t key_info = read_be16(eapol + 5u);
    if ((key_info & EAPOL_KEY_INFO_VERSION_MASK) != WPA2_KEY_INFO_VERSION) {
        return WPA2_HANDSHAKE_IGNORE;
    }
    const uint8_t *replay = eapol + 9u;
    const uint8_t *nonce = eapol + 17u;
    if ((key_info & EAPOL_KEY_INFO_PAIRWISE) == 0u) {
        return process_group_key_message(handshake, eapol, eapol_length,
                                         key_data_length, response,
                                         response_capacity, response_length);
    }

    if (key_info == (WPA2_KEY_INFO_VERSION |
                     EAPOL_KEY_INFO_PAIRWISE | EAPOL_KEY_INFO_ACK)) {
        if (key_data_length != 0u) {
            return WPA2_HANDSHAKE_IGNORE;
        }
        if ((handshake->state == WPA2_HANDSHAKE_WAIT_M3 ||
             handshake->state == WPA2_HANDSHAKE_WAIT_M3_REKEY) &&
            replay_counter_equal(replay, handshake->replay_counter) &&
            bytes_equal(nonce, handshake->authenticator_nonce, 32u)) {
            return build_key_response(
                       handshake->state == WPA2_HANDSHAKE_WAIT_M3_REKEY
                           ? handshake->pending_ptk
                           : handshake->ptk,
                       WPA2_KEY_INFO_VERSION |
                                      EAPOL_KEY_INFO_PAIRWISE |
                                      EAPOL_KEY_INFO_MIC,
                       replay, handshake->supplicant_nonce, 0, 0u, response,
                       response_capacity,
                       response_length)
                       ? WPA2_HANDSHAKE_RESPONSE
                       : WPA2_HANDSHAKE_ERROR;
        }
        int rekey = handshake->state == WPA2_HANDSHAKE_ESTABLISHED;
        if (handshake->state != WPA2_HANDSHAKE_WAIT_M1 && !rekey) {
            return WPA2_HANDSHAKE_IGNORE;
        }
        if (rekey &&
            (read_be64(replay) <=
                 read_be64(handshake->last_m3_replay_counter) ||
             handshake->random_fill == 0)) {
            return WPA2_HANDSHAKE_IGNORE;
        }
        for (uint32_t index = 0u; index < 32u; ++index) {
            handshake->authenticator_nonce[index] = nonce[index];
        }
        if (!generate_snonce(handshake, replay)) {
            handshake->state = WPA2_HANDSHAKE_FAILED;
            return WPA2_HANDSHAKE_ERROR;
        }
        uint8_t *candidate_ptk =
            rekey != 0 ? handshake->pending_ptk : handshake->ptk;
        if (!wpa2_derive_ptk(handshake->pmk,
                             handshake->authenticator_address,
                             handshake->supplicant_address,
                             handshake->authenticator_nonce,
                             handshake->supplicant_nonce,
                             candidate_ptk)) {
            handshake->state = WPA2_HANDSHAKE_FAILED;
            return WPA2_HANDSHAKE_ERROR;
        }
        for (uint32_t index = 0u; index < 8u; ++index) {
            handshake->replay_counter[index] = replay[index];
        }
        if (!build_key_response(candidate_ptk,
                                WPA2_KEY_INFO_VERSION |
                                    EAPOL_KEY_INFO_PAIRWISE |
                                    EAPOL_KEY_INFO_MIC,
                                replay, handshake->supplicant_nonce, 0, 0u,
                                response, response_capacity,
                                response_length)) {
            handshake->state = WPA2_HANDSHAKE_FAILED;
            return WPA2_HANDSHAKE_ERROR;
        }
        handshake->state = rekey != 0 ? WPA2_HANDSHAKE_WAIT_M3_REKEY
                                      : WPA2_HANDSHAKE_WAIT_M3;
        return WPA2_HANDSHAKE_RESPONSE;
    }

    uint16_t expected_m3_info = WPA2_KEY_INFO_VERSION |
                                EAPOL_KEY_INFO_PAIRWISE |
                                EAPOL_KEY_INFO_INSTALL |
                                EAPOL_KEY_INFO_ACK | EAPOL_KEY_INFO_MIC |
                                EAPOL_KEY_INFO_SECURE |
                                EAPOL_KEY_INFO_ENCRYPTED_DATA;
    if (key_info != expected_m3_info ||
        key_data_length == 0u ||
        (handshake->state != WPA2_HANDSHAKE_WAIT_M3 &&
         handshake->state != WPA2_HANDSHAKE_WAIT_M3_REKEY &&
         handshake->state != WPA2_HANDSHAKE_ESTABLISHED) ||
        !bytes_equal(nonce, handshake->authenticator_nonce, 32u) ||
        read_be64(replay) < read_be64(handshake->replay_counter) ||
        !verify_eapol_mic(
            handshake->state == WPA2_HANDSHAKE_WAIT_M3_REKEY
                ? handshake->pending_ptk
                : handshake->ptk,
            eapol, eapol_length)) {
        return WPA2_HANDSHAKE_ERROR;
    }

    if (handshake->state == WPA2_HANDSHAKE_ESTABLISHED &&
        replay_counter_equal(replay, handshake->last_m3_replay_counter) &&
        handshake->last_m4_length != 0u) {
        if (response_capacity < handshake->last_m4_length) {
            return WPA2_HANDSHAKE_ERROR;
        }
        for (uint32_t index = 0u; index < handshake->last_m4_length; ++index) {
            response[index] = handshake->last_m4[index];
        }
        *response_length = handshake->last_m4_length;
        return WPA2_HANDSHAKE_COMPLETE;
    }
    if ((handshake->state == WPA2_HANDSHAKE_WAIT_M3 &&
         read_be64(replay) <= read_be64(handshake->replay_counter)) ||
        (handshake->state == WPA2_HANDSHAKE_WAIT_M3_REKEY &&
         read_be64(replay) <= read_be64(handshake->replay_counter)) ||
        (handshake->state == WPA2_HANDSHAKE_ESTABLISHED &&
         read_be64(replay) <=
             read_be64(handshake->last_m3_replay_counter)) ||
        handshake->gtk_generation == UINT32_MAX ||
        (handshake->state != WPA2_HANDSHAKE_ESTABLISHED &&
         handshake->ptk_generation == UINT32_MAX)) {
        return WPA2_HANDSHAKE_ERROR;
    }

    uint8_t key_data[WPA2_EAPOL_MAX_SIZE];
    uint8_t gtk_candidate[WPA2_GTK_SIZE];
    uint8_t gtk_id_candidate = 0u;
    uint32_t plain_length = 0u;
    uint8_t *candidate_ptk =
        handshake->state == WPA2_HANDSHAKE_WAIT_M3_REKEY
            ? handshake->pending_ptk
            : handshake->ptk;
    if (!wpa2_aes_unwrap(candidate_ptk + 16u,
                         eapol + EAPOL_KEY_DATA_OFFSET, key_data_length,
                         key_data, sizeof(key_data), &plain_length) ||
        !parse_gtk_kde(key_data, plain_length, gtk_candidate,
                       &gtk_id_candidate)) {
        clear_bytes(key_data, sizeof(key_data));
        clear_bytes(gtk_candidate, sizeof(gtk_candidate));
        handshake->state = WPA2_HANDSHAKE_FAILED;
        return WPA2_HANDSHAKE_ERROR;
    }
    clear_bytes(key_data, sizeof(key_data));
    for (uint32_t index = 0u; index < WPA2_GTK_SIZE; ++index) {
        handshake->gtk[index] = gtk_candidate[index];
    }
    clear_bytes(gtk_candidate, sizeof(gtk_candidate));
    handshake->gtk_id = gtk_id_candidate;
    handshake->gtk_valid = 1u;
    ++handshake->gtk_generation;
    for (uint32_t index = 0u; index < 8u; ++index) {
        handshake->last_m3_replay_counter[index] = replay[index];
    }

    uint32_t m4_length = 0u;
    uint32_t rekey = handshake->state == WPA2_HANDSHAKE_WAIT_M3_REKEY;
    if (!build_key_response(candidate_ptk,
                            WPA2_KEY_INFO_VERSION |
                                EAPOL_KEY_INFO_PAIRWISE |
                                EAPOL_KEY_INFO_MIC |
                                EAPOL_KEY_INFO_SECURE,
                            replay, handshake->supplicant_nonce, 0, 0u,
                            response, response_capacity,
                            &m4_length)) {
        handshake->state = WPA2_HANDSHAKE_FAILED;
        return WPA2_HANDSHAKE_ERROR;
    }
    for (uint32_t index = 0u; index < m4_length; ++index) {
        handshake->last_m4[index] = response[index];
    }
    handshake->last_m4_length = (uint16_t)m4_length;
    if (rekey != 0u) {
        for (uint32_t index = 0u; index < sizeof(handshake->ptk); ++index) {
            handshake->ptk[index] = handshake->pending_ptk[index];
        }
        clear_bytes(handshake->pending_ptk, sizeof(handshake->pending_ptk));
    }
    ++handshake->ptk_generation;
    handshake->state = WPA2_HANDSHAKE_ESTABLISHED;
    *response_length = m4_length;
    return WPA2_HANDSHAKE_COMPLETE;
}
