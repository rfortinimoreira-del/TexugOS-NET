#ifndef TEXUGOS_RTL8192SE_H
#define TEXUGOS_RTL8192SE_H

#include <stdint.h>

struct rtl8192se_firmware;
struct rtl8192se_scan_result;

enum rtl8192se_association_state {
    RTL8192SE_ASSOCIATION_FAILED = -1,
    RTL8192SE_ASSOCIATION_IDLE = 0,
    RTL8192SE_ASSOCIATION_AUTHENTICATING = 1,
    RTL8192SE_ASSOCIATION_ASSOCIATING = 2,
    RTL8192SE_ASSOCIATION_CONNECTED_OPEN = 3,
    RTL8192SE_ASSOCIATION_HANDSHAKING = 4,
    RTL8192SE_ASSOCIATION_CONNECTED_WPA2 = 5,
};

enum rtl8192se_association_failure {
    RTL8192SE_ASSOCIATION_FAILURE_NONE = 0,
    RTL8192SE_ASSOCIATION_FAILURE_AUTH_TIMEOUT,
    RTL8192SE_ASSOCIATION_FAILURE_AUTH_REJECTED,
    RTL8192SE_ASSOCIATION_FAILURE_ASSOC_TIMEOUT,
    RTL8192SE_ASSOCIATION_FAILURE_ASSOC_REJECTED,
    RTL8192SE_ASSOCIATION_FAILURE_WPA2_TIMEOUT,
    RTL8192SE_ASSOCIATION_FAILURE_WPA2_HANDSHAKE,
    RTL8192SE_ASSOCIATION_FAILURE_TX,
    RTL8192SE_ASSOCIATION_FAILURE_TX_STUCK,
};

enum rtl8192se_scan_state {
    RTL8192SE_SCAN_FAILED = -1,
    RTL8192SE_SCAN_IDLE = 0,
    RTL8192SE_SCAN_ACTIVE = 1,
    RTL8192SE_SCAN_COMPLETE = 2,
};

enum rtl8192se_firmware_upload_failure {
    RTL8192SE_FW_UPLOAD_OK = 0,
    RTL8192SE_FW_UPLOAD_INVALID_INPUT,
    RTL8192SE_FW_UPLOAD_IMEM_TRANSFER,
    RTL8192SE_FW_UPLOAD_IMEM_CHECK,
    RTL8192SE_FW_UPLOAD_EMEM_TRANSFER,
    RTL8192SE_FW_UPLOAD_EMEM_CHECK,
    RTL8192SE_FW_UPLOAD_CPU_READY,
    RTL8192SE_FW_UPLOAD_DMEM_TRANSFER,
    RTL8192SE_FW_UPLOAD_DMEM_CHECK,
    RTL8192SE_FW_UPLOAD_FIRMWARE_READY,
};

struct rtl8192se_status {
    uint16_t device_id;
    uint32_t mmio_base;
    uint32_t command_ring;
    uint32_t rx_mpdu_ring;
    uint32_t rx_command_ring;
    uint32_t tx_data_ring;
    uint32_t tx_management_ring;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t probed;
    uint8_t dma_prepared;
    uint8_t rx_dma_prepared;
    uint8_t tx_dma_prepared;
    uint8_t mac_power_ready;
    uint8_t phy_initialized;
    uint8_t rx_rings_programmed;
    uint8_t tx_rings_programmed;
    uint8_t mac_io_enabled;
    uint8_t adapter_config_read;
    uint8_t mac_address[6];
    uint8_t rf_configuration;
    uint8_t channel_plan;
    uint8_t firmware_uploaded;
    uint8_t channel;
    enum rtl8192se_firmware_upload_failure firmware_upload_failure;
    uint32_t firmware_upload_tcr;
};

int rtl8192se_probe(struct rtl8192se_status *status);
int rtl8192se_prepare_command_dma(struct rtl8192se_status *status);
int rtl8192se_build_command_ring(uint8_t *ring, uint32_t physical_address,
                                uint32_t descriptor_count);
int rtl8192se_start_firmware(struct rtl8192se_status *status,
                             const struct rtl8192se_firmware *firmware);
int rtl8192se_send_80211_frame(const uint8_t *frame, uint32_t frame_length,
                               int management_frame);
int rtl8192se_receive_80211_frame(uint8_t *frame, uint32_t frame_capacity,
                                  uint32_t *frame_length);
void rtl8192se_mark_disconnected(void);
int rtl8192se_network_is_associated(void);
int rtl8192se_network_is_wpa2(void);
int rtl8192se_open_association_start(
    const struct rtl8192se_scan_result *network);
int rtl8192se_wpa2_association_start(
    const struct rtl8192se_scan_result *network,
    const uint8_t *passphrase, uint32_t passphrase_length);
enum rtl8192se_association_state rtl8192se_open_association_poll(void);
enum rtl8192se_association_failure
rtl8192se_association_last_failure(void);
uint32_t rtl8192se_association_rx_frame_count(void);
uint32_t rtl8192se_association_auth_response_count(void);
uint32_t rtl8192se_association_auth_tx_submission_count(void);
uint32_t rtl8192se_association_auth_tx_dma_completion_count(void);
uint32_t rtl8192se_association_rx_invalid_descriptor_count(void);
int rtl8192se_scan_start(void);
int rtl8192se_scan_for_ssid_start(const uint8_t *ssid, uint8_t ssid_length);
enum rtl8192se_scan_state rtl8192se_scan_poll(void);
uint32_t rtl8192se_scan_result_count(void);
uint32_t rtl8192se_scan_received_frame_count(void);
uint32_t rtl8192se_scan_parsed_frame_count(void);
uint32_t rtl8192se_scan_management_frame_count(void);
uint32_t rtl8192se_scan_beacon_frame_count(void);
uint32_t rtl8192se_scan_probe_response_frame_count(void);
uint32_t rtl8192se_scan_other_frame_count(void);
uint32_t rtl8192se_scan_short_frame_count(void);
int rtl8192se_scan_get_result(uint32_t index,
                              struct rtl8192se_scan_result *result);
int rtl8192se_network_send(const void *frame, uint16_t length);
int rtl8192se_network_receive(void *frame, uint16_t capacity,
                              uint16_t *length);
void rtl8192se_network_get_mac(uint8_t address[6]);

#endif
