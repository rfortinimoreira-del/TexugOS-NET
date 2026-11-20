#ifndef TEXUGOS_WPA2_HANDSHAKE_H
#define TEXUGOS_WPA2_HANDSHAKE_H

#include <stdint.h>

#define WPA2_EAPOL_MAX_SIZE 512u

typedef int (*wpa2_random_fill_fn)(void *context, uint8_t *buffer,
                                   uint32_t length);

enum wpa2_handshake_state {
    WPA2_HANDSHAKE_IDLE = 0,
    WPA2_HANDSHAKE_WAIT_M1 = 1,
    WPA2_HANDSHAKE_WAIT_M3 = 2,
    WPA2_HANDSHAKE_ESTABLISHED = 3,
    WPA2_HANDSHAKE_FAILED = 4,
    WPA2_HANDSHAKE_WAIT_M3_REKEY = 5,
};

enum wpa2_handshake_result {
    WPA2_HANDSHAKE_ERROR = -1,
    WPA2_HANDSHAKE_IGNORE = 0,
    WPA2_HANDSHAKE_RESPONSE = 1,
    WPA2_HANDSHAKE_COMPLETE = 2,
};

struct wpa2_handshake {
    uint8_t pmk[32];
    uint8_t ptk[64];
    uint8_t pending_ptk[64];
    uint8_t authenticator_address[6];
    uint8_t supplicant_address[6];
    uint8_t authenticator_nonce[32];
    uint8_t supplicant_nonce[32];
    uint8_t replay_counter[8];
    uint8_t gtk[16];
    uint8_t gtk_id;
    uint8_t gtk_valid;
    uint8_t last_m3_replay_counter[8];
    uint8_t last_m4[WPA2_EAPOL_MAX_SIZE];
    uint16_t last_m4_length;
    uint8_t last_group_replay_counter[8];
    uint8_t last_group_m2[WPA2_EAPOL_MAX_SIZE];
    uint16_t last_group_m2_length;
    uint32_t gtk_generation;
    uint32_t ptk_generation;
    wpa2_random_fill_fn random_fill;
    void *random_context;
    enum wpa2_handshake_state state;
};

int wpa2_handshake_begin(struct wpa2_handshake *handshake,
                         const uint8_t *passphrase,
                         uint32_t passphrase_length, const uint8_t *ssid,
                         uint32_t ssid_length,
                         const uint8_t authenticator_address[6],
                         const uint8_t supplicant_address[6],
                         wpa2_random_fill_fn random_fill,
                         void *random_context);
enum wpa2_handshake_result wpa2_handshake_process(
    struct wpa2_handshake *handshake, const uint8_t *eapol,
    uint32_t eapol_length, uint8_t *response, uint32_t response_capacity,
    uint32_t *response_length);
void wpa2_handshake_clear(struct wpa2_handshake *handshake);

#endif
