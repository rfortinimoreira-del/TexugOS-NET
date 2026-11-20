#include "rtl8192se_hw.h"
#include "rtl8192se_firmware.h"
#include "rtl8192se_tables.h"

#define SYS_FUNC_EN 0x0002u
#define SYS_CLKR 0x0008u
#define AFE_MISC 0x0010u
#define LDOA15_CTRL 0x0020u
#define AFE_XTAL_CTRL 0x0026u
#define AFE_PLL_CTRL 0x0028u
#define SYS_ISO_CTRL 0x0000u
#define EFUSE_CONTROL 0x0030u
#define EFUSE_TEST 0x0034u
#define EFUSE_PHYSICAL_SIZE 512u
#define EFUSE_LOGICAL_SIZE 128u
#define EFUSE_SECTION_COUNT 16u
#define EFUSE_WORD_COUNT 4u
#define RX_DESCRIPTOR_SIZE 32u
#define RX_DESCRIPTOR_COUNT_MAX 64u
#define RX_DESCRIPTOR_OWN (1u << 31)
#define RX_DESCRIPTOR_END_OF_RING (1u << 30)
#define RX_DESCRIPTOR_CRC_ERROR (1u << 14)
#define RX_DESCRIPTOR_ICV_ERROR (1u << 15)
#define RX_DESCRIPTOR_LENGTH_MASK 0x3fffu
#define RX_DESCRIPTOR_INFO_LENGTH_MASK (0x0fu << 16)
#define RX_DESCRIPTOR_BUFFER_SHIFT_MASK (0x03u << 24)
#define RX_DESCRIPTOR_BUFFER_ADDRESS_OFFSET 24u
#define RX_FRAME_FCS_LENGTH 4u
#define RX_BUFFER_MAX_SIZE 0x3fffu
#define RX_DMA_ADDRESS_LIMIT 0x01000000u
#define RX_MPDU_RING_ADDRESS 0x053cu
#define RX_COMMAND_RING_ADDRESS 0x0538u
#define TX_MANAGEMENT_RING_ADDRESS 0x0518u
#define TX_DATA_RING_ADDRESS 0x052cu
#define TX_POLL 0x0500u
#define RXDMA 0x00bdu
#define RX_DRIVER_INFO_SIZE 0x00b6u
#define RECEIVE_CONFIG 0x0048u
#define MAC_IO_ENABLE 0x37fcu
#define MAC_IO_ENABLE_MASK 0x37fcu
#define RXDMA_ARBITRATION (1u << 6)
#define RECEIVE_ALL 0x031f000fu
#define EEPROM_SIGNATURE 0x8129u
#define EEPROM_SIGNATURE_OFFSET 0x00u
#define EEPROM_MAC_ADDRESS 0x12u
#define EEPROM_TX_POWER_BASE 0x50u
#define EEPROM_CHANNEL_PLAN 0x7bu
#define EEPROM_BOARD_TYPE 0x7eu
#define BB_TX_POWER_CCK 0x0e08u
#define BB_TX_POWER_CCK_MASK 0x00007f00u
#define RF_POWER_CONTROL 0x0561u
#define WLAN_MANAGEMENT_HEADER_SIZE 24u
#define WLAN_BEACON_FIXED_SIZE 12u
#define WLAN_AUTH_FRAME_SIZE 30u
#define WLAN_ASSOC_FIXED_SIZE 28u
#define WLAN_ASSOC_RESPONSE_FIXED_SIZE 30u
#define WLAN_SSID_ELEMENT 0u
#define WLAN_CHANNEL_ELEMENT 3u
#define WLAN_RSN_ELEMENT 48u
#define WLAN_VENDOR_ELEMENT 221u
#define WLAN_SUPPORTED_RATES_ELEMENT 1u
#define WLAN_EXTENDED_RATES_ELEMENT 50u
#define WLAN_DATA_HEADER_SIZE 24u
#define WLAN_QOS_HEADER_SIZE 26u
#define WLAN_LLC_SNAP_SIZE 8u
#define ETHERNET_HEADER_SIZE 14u
#define COMMAND 0x0040u
#define QUEUE_PAGE_NUMBER 0x00a0u
#define PCI_FUNCTION_CONTROL 0x0503u
#define DEBUG_PORT 0x003au
#define TCR 0x0044u
#define TX_COMMAND_DESCRIPTOR 0x051cu
#define RQPN 0x00a0u
#define FEN_CPUEN (1u << 10)
#define SYS_CPU_CLKSEL (1u << 2)
#define IMEM_RDY (1u << 5)
#define IMEM_CODE_DONE (1u << 0)
#define IMEM_CHECK_REPORT (1u << 1)
#define EMEM_CODE_DONE (1u << 2)
#define EMEM_CHECK_REPORT (1u << 3)
#define DMEM_CODE_DONE (1u << 4)
#define FIRMWARE_READY 0xffu
#define COMMAND_QUEUE_POLL (1u << 5)
#define COMMAND_QUEUE_COUNT 16u
#define COMMAND_DESCRIPTOR_SIZE 64u
#define DMA_CHUNK_SIZE 4096u
#define DMA_ADDRESS_LIMIT 0x01000000u
#define TCR_DMA_INIT_VALUE (IMEM_CHECK_REPORT | (1u << 3))
#define COMMAND_DMA_ENABLE 0x37fcu
#define COMMAND_DMA_PREPARE 0x07fcu
#define TX_DESCRIPTOR_MASK 0x00dcu
#define INITIAL_RATE_SELECTION 0x0160u
#define SYS_FUNC_ENABLE_CPU_BIT (1u << 10)
#define SYS_CLKR_CPU_BIT (1u << 2)
#define RF_REGISTER_CHANNEL_BANDWIDTH 0x18u
#define RF_REGISTER_MASK 0x000fffffu
#define RF_CHANNEL_MASK 0x000003ffu
#define RF_BANDWIDTH_20MHZ 0x00000400u
#define RF_INTERFACE_ENABLE_MASK (0x10u << 16)
#define RF_INTERFACE_ENABLE_BIT 0x10u
#define RF_HSSI_ADDRESS_LENGTH 0x400u
#define RF_HSSI_DATA_LENGTH 0x800u
#define BB_RF_PATH_A_HSSI2 0x824u
#define BB_RF_PATH_B_HSSI2 0x82cu
#define BB_RF_PATH_A_LSSI 0x840u
#define BB_RF_PATH_B_LSSI 0x844u
#define BB_RF_PATH_A_INTERFACE 0x860u
#define BB_RF_PATH_B_INTERFACE 0x864u
#define BB_RF_INTERFACE_SWITCH 0x870u
#define BB_RF_MODE_0 0x800u
#define BB_RF_MODE_1 0x900u
#define BB_RX_PATH_ENABLE 0xc04u
#define MAC_BANDWIDTH_OPERATION 0x203u
#define MAC_FUNCTION_CONTROL 0x001fu
#define MAC_ADDRESS_REGISTER 0x0050u
#define RF_READ_ADDRESS_MASK 0x7f800000u
#define RF_READ_EDGE 0x80000000u
#define RF_READBACK_MASK 0x000fffffu
#define RF_READBACK_PATH_A 0x8a0u
#define RF_READBACK_PATH_B 0x8a4u
#define RF_READBACK_PATH_A_PI 0x8b8u
#define RF_READBACK_PATH_B_PI 0x8bcu
#define RF_HSSI_PATH_A 0x820u
#define RF_HSSI_PATH_B 0x828u

static uint32_t rf_channel_bandwidth[2];
static uint8_t rf_path_count;
static uint8_t phy_initialized;
static uint8_t cck_tx_power[3];
static uint8_t cck_tx_power_valid_mask;

static uint8_t read8(const struct rtl8192se_register_io *io, uint16_t offset)
{
    return io->read8(io->context, offset);
}

static void write8(const struct rtl8192se_register_io *io, uint16_t offset,
                   uint8_t value)
{
    io->write8(io->context, offset, value);
}

static uint16_t read16(const struct rtl8192se_register_io *io, uint16_t offset)
{
    return io->read16(io->context, offset);
}

static void write16(const struct rtl8192se_register_io *io, uint16_t offset,
                    uint16_t value)
{
    io->write16(io->context, offset, value);
}

static uint32_t read32(const struct rtl8192se_register_io *io, uint16_t offset)
{
    return io->read32(io->context, offset);
}

static void write32(const struct rtl8192se_register_io *io, uint16_t offset,
                    uint32_t value)
{
    io->write32(io->context, offset, value);
}

static uint32_t read_le32(const volatile uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static int valid_mac_address(const uint8_t *address)
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

static int wlan_copy_bytes(uint8_t *destination, const uint8_t *source,
                           uint32_t length);

static int wlan_suite_matches(const uint8_t *suite, uint8_t type)
{
    return suite[0] == 0x00u && suite[1] == 0x0fu &&
           suite[2] == 0xacu && suite[3] == type;
}

static int wlan_parse_rsn(const uint8_t *element, uint8_t element_length,
                          uint8_t *supports_psk_ccmp)
{
    uint32_t offset = 0u;
    if (supports_psk_ccmp == 0 || element_length < 8u ||
        ((uint16_t)element[0] | ((uint16_t)element[1] << 8)) != 1u) {
        return 0;
    }
    offset = 2u;
    int group_ccmp = wlan_suite_matches(element + offset, 4u);
    offset += 4u;

    uint32_t pairwise_count =
        (uint32_t)element[offset] | ((uint32_t)element[offset + 1u] << 8);
    offset += 2u;
    if (pairwise_count == 0u ||
        pairwise_count > (element_length - offset) / 4u) {
        return 0;
    }
    int pairwise_ccmp = 0;
    for (uint32_t index = 0u; index < pairwise_count; ++index) {
        pairwise_ccmp |= wlan_suite_matches(element + offset, 4u);
        offset += 4u;
    }

    if (element_length - offset < 2u) {
        return 0;
    }
    uint32_t akm_count =
        (uint32_t)element[offset] | ((uint32_t)element[offset + 1u] << 8);
    offset += 2u;
    if (akm_count == 0u || akm_count > (element_length - offset) / 4u) {
        return 0;
    }
    int psk_akm = 0;
    for (uint32_t index = 0u; index < akm_count; ++index) {
        psk_akm |= wlan_suite_matches(element + offset, 2u);
        offset += 4u;
    }
    *supports_psk_ccmp =
        (uint8_t)(group_ccmp && pairwise_ccmp && psk_akm);
    return 1;
}

int rtl8192se_parse_scan_frame(const uint8_t *frame, uint32_t frame_length,
                               struct rtl8192se_scan_result *result)
{
    if (frame == 0 || result == 0 ||
        frame_length < WLAN_MANAGEMENT_HEADER_SIZE + WLAN_BEACON_FIXED_SIZE ||
        (frame[0] & 0x0fu) != 0u) {
        return 0;
    }

    uint8_t subtype = frame[0] >> 4;
    if (subtype != 5u && subtype != 8u) {
        return 0;
    }

    uint32_t information_offset =
        WLAN_MANAGEMENT_HEADER_SIZE + WLAN_BEACON_FIXED_SIZE;
    uint16_t capabilities =
        (uint16_t)frame[34u] | ((uint16_t)frame[35u] << 8);
    uint8_t has_ssid = 0u;
    uint8_t has_channel = 0u;
    uint8_t has_rsn = 0u;
    uint8_t has_wpa_vendor = 0u;
    uint8_t supports_psk_ccmp = 0u;
    struct rtl8192se_scan_result parsed = {0};
    if (!valid_mac_address(frame + 16u)) {
        return 0;
    }
    for (uint32_t index = 0u; index < 6u; ++index) {
        parsed.bssid[index] = frame[16u + index];
    }

    while (information_offset < frame_length) {
        if (frame_length - information_offset < 2u) {
            return 0;
        }
        uint8_t element_id = frame[information_offset];
        uint8_t element_length = frame[information_offset + 1u];
        information_offset += 2u;
        if (element_length > frame_length - information_offset) {
            return 0;
        }
        const uint8_t *element = frame + information_offset;
        if (element_id == WLAN_SSID_ELEMENT) {
            if (has_ssid != 0u || element_length > sizeof(parsed.ssid)) {
                return 0;
            }
            has_ssid = 1u;
            parsed.ssid_length = element_length;
            for (uint32_t index = 0u; index < element_length; ++index) {
                parsed.ssid[index] = element[index];
            }
        } else if (element_id == WLAN_CHANNEL_ELEMENT) {
            if (has_channel != 0u || element_length != 1u ||
                element[0] == 0u || element[0] > 14u) {
                return 0;
            }
            has_channel = 1u;
            parsed.channel = element[0];
        } else if (element_id == WLAN_RSN_ELEMENT) {
            if (has_rsn != 0u ||
                !wlan_parse_rsn(element, element_length,
                                &supports_psk_ccmp)) {
                return 0;
            }
            has_rsn = 1u;
        } else if (element_id == WLAN_VENDOR_ELEMENT &&
                   element_length >= 4u && element[0] == 0x00u &&
                   element[1] == 0x50u && element[2] == 0xf2u &&
                   element[3] == 0x01u) {
            has_wpa_vendor = 1u;
        }
        information_offset += element_length;
    }

    if (has_ssid == 0u) {
        return 0;
    }
    if ((capabilities & (1u << 4)) == 0u && has_rsn == 0u &&
        has_wpa_vendor == 0u) {
        parsed.security = RTL8192SE_SECURITY_OPEN;
    } else if ((capabilities & (1u << 4)) != 0u && has_rsn != 0u &&
               supports_psk_ccmp != 0u) {
        parsed.security = RTL8192SE_SECURITY_WPA2_PSK_CCMP;
    } else {
        parsed.security = RTL8192SE_SECURITY_UNSUPPORTED;
    }
    *result = parsed;
    return 1;
}

static int rtl8192se_build_probe_request_internal(
    const uint8_t station_address[6], const uint8_t *ssid,
    uint8_t ssid_length, uint16_t sequence_number, uint8_t *frame,
    uint32_t capacity, uint32_t *frame_length)
{
    static const uint8_t broadcast[6] = {
        0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
    };
    static const uint8_t supported_rates[8] = {
        0x82u, 0x84u, 0x8bu, 0x96u, 0x0cu, 0x12u, 0x18u, 0x24u,
    };
    static const uint8_t extended_rates[4] = {
        0x30u, 0x48u, 0x60u, 0x6cu,
    };
    const uint32_t required_capacity = WLAN_MANAGEMENT_HEADER_SIZE + 2u +
        ssid_length + 2u + sizeof(supported_rates) +
        2u + sizeof(extended_rates);
    if (station_address == 0 || frame == 0 || frame_length == 0 ||
        (ssid_length != 0u && ssid == 0) || ssid_length > 32u ||
        capacity < required_capacity ||
        !valid_mac_address(station_address) || sequence_number > 0x0fffu) {
        return 0;
    }

    frame[0] = 0x40u;
    frame[1] = 0u;
    frame[2] = 0u;
    frame[3] = 0u;
    wlan_copy_bytes(frame + 4u, broadcast, 6u);
    wlan_copy_bytes(frame + 10u, station_address, 6u);
    wlan_copy_bytes(frame + 16u, broadcast, 6u);
    frame[22] = (uint8_t)(sequence_number << 4);
    frame[23] = (uint8_t)(sequence_number >> 4);
    uint32_t offset = WLAN_MANAGEMENT_HEADER_SIZE;
    frame[offset++] = WLAN_SSID_ELEMENT;
    frame[offset++] = ssid_length;
    for (uint32_t index = 0u; index < ssid_length; ++index) {
        frame[offset++] = ssid[index];
    }
    frame[offset++] = WLAN_SUPPORTED_RATES_ELEMENT;
    frame[offset++] = sizeof(supported_rates);
    wlan_copy_bytes(frame + offset, supported_rates,
                    sizeof(supported_rates));
    offset += sizeof(supported_rates);
    frame[offset++] = WLAN_EXTENDED_RATES_ELEMENT;
    frame[offset++] = sizeof(extended_rates);
    wlan_copy_bytes(frame + offset, extended_rates, sizeof(extended_rates));
    offset += sizeof(extended_rates);
    *frame_length = offset;
    return 1;
}

int rtl8192se_build_probe_request(const uint8_t station_address[6],
                                  uint16_t sequence_number, uint8_t *frame,
                                  uint32_t capacity,
                                  uint32_t *frame_length)
{
    return rtl8192se_build_probe_request_internal(
        station_address, 0, 0u, sequence_number, frame, capacity,
        frame_length);
}

int rtl8192se_build_probe_request_for_ssid(
    const uint8_t station_address[6], const uint8_t *ssid,
    uint8_t ssid_length, uint16_t sequence_number, uint8_t *frame,
    uint32_t capacity, uint32_t *frame_length)
{
    return ssid_length != 0u && rtl8192se_build_probe_request_internal(
        station_address, ssid, ssid_length, sequence_number, frame,
        capacity, frame_length);
}

static int wlan_copy_bytes(uint8_t *destination, const uint8_t *source,
                           uint32_t length)
{
    if (destination == 0 || source == 0) {
        return 0;
    }
    for (uint32_t index = 0u; index < length; ++index) {
        destination[index] = source[index];
    }
    return 1;
}

static int wlan_bytes_equal(const uint8_t *left, const uint8_t *right,
                            uint32_t length)
{
    for (uint32_t index = 0u; index < length; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

static int wlan_write_management_header(uint8_t *frame, uint32_t capacity,
                                        uint8_t frame_control,
                                        const uint8_t bssid[6],
                                        const uint8_t station_address[6],
                                        uint16_t sequence_number)
{
    if (frame == 0 || bssid == 0 || station_address == 0 ||
        capacity < WLAN_MANAGEMENT_HEADER_SIZE ||
        !valid_mac_address(bssid) || !valid_mac_address(station_address) ||
        sequence_number > 0x0fffu) {
        return 0;
    }
    frame[0] = frame_control;
    frame[1] = 0u;
    frame[2] = 0u;
    frame[3] = 0u;
    wlan_copy_bytes(frame + 4u, bssid, 6u);
    wlan_copy_bytes(frame + 10u, station_address, 6u);
    wlan_copy_bytes(frame + 16u, bssid, 6u);
    frame[22] = (uint8_t)(sequence_number << 4);
    frame[23] = (uint8_t)(sequence_number >> 4);
    return 1;
}

int rtl8192se_build_open_auth_request(const uint8_t bssid[6],
                                      const uint8_t station_address[6],
                                      uint16_t sequence_number,
                                      uint8_t *frame, uint32_t capacity,
                                      uint32_t *frame_length)
{
    if (frame_length == 0 || capacity < WLAN_AUTH_FRAME_SIZE ||
        !wlan_write_management_header(frame, capacity, 0xb0u, bssid,
                                      station_address, sequence_number)) {
        return 0;
    }
    frame[24] = 0u;
    frame[25] = 0u;
    frame[26] = 1u;
    frame[27] = 0u;
    frame[28] = 0u;
    frame[29] = 0u;
    *frame_length = WLAN_AUTH_FRAME_SIZE;
    return 1;
}

int rtl8192se_parse_open_auth_response(const uint8_t *frame,
                                       uint32_t frame_length,
                                       const uint8_t bssid[6],
                                       const uint8_t station_address[6],
                                       uint8_t *authenticated)
{
    if (frame == 0 || bssid == 0 || station_address == 0 ||
        authenticated == 0 ||
        frame_length < WLAN_AUTH_FRAME_SIZE ||
        (frame[0] & 0xfcu) != 0xb0u || (frame[1] & 0x03u) != 0u ||
        !wlan_bytes_equal(frame + 4u, station_address, 6u) ||
        !wlan_bytes_equal(frame + 10u, bssid, 6u) ||
        !wlan_bytes_equal(frame + 16u, bssid, 6u)) {
        return 0;
    }
    *authenticated = (uint8_t)(
        frame[24] == 0u && frame[25] == 0u &&
        frame[26] == 2u && frame[27] == 0u &&
        frame[28] == 0u && frame[29] == 0u);
    return 1;
}

int rtl8192se_build_open_assoc_request(const uint8_t bssid[6],
                                       const uint8_t station_address[6],
                                       const uint8_t *ssid,
                                       uint8_t ssid_length,
                                       uint16_t sequence_number,
                                       uint8_t *frame, uint32_t capacity,
                                       uint32_t *frame_length)
{
    static const uint8_t supported_rates[8] = {
        0x82u, 0x84u, 0x8bu, 0x96u, 0x0cu, 0x12u, 0x18u, 0x24u,
    };
    static const uint8_t extended_rates[4] = {
        0x30u, 0x48u, 0x60u, 0x6cu,
    };
    uint32_t required_capacity = WLAN_ASSOC_FIXED_SIZE + 2u + ssid_length +
                                 2u + sizeof(supported_rates) + 2u +
                                 sizeof(extended_rates);
    if (frame_length == 0 || ssid == 0 || ssid_length == 0u ||
        ssid_length > 32u || capacity < required_capacity ||
        !wlan_write_management_header(frame, capacity, 0x00u, bssid,
                                      station_address, sequence_number)) {
        return 0;
    }
    frame[24] = 0x01u;
    frame[25] = 0x00u;
    frame[26] = 10u;
    frame[27] = 0u;
    uint32_t offset = WLAN_ASSOC_FIXED_SIZE;
    frame[offset++] = WLAN_SSID_ELEMENT;
    frame[offset++] = ssid_length;
    wlan_copy_bytes(frame + offset, ssid, ssid_length);
    offset += ssid_length;
    frame[offset++] = WLAN_SUPPORTED_RATES_ELEMENT;
    frame[offset++] = sizeof(supported_rates);
    wlan_copy_bytes(frame + offset, supported_rates,
                    sizeof(supported_rates));
    offset += sizeof(supported_rates);
    frame[offset++] = WLAN_EXTENDED_RATES_ELEMENT;
    frame[offset++] = sizeof(extended_rates);
    wlan_copy_bytes(frame + offset, extended_rates, sizeof(extended_rates));
    offset += sizeof(extended_rates);
    *frame_length = offset;
    return 1;
}

int rtl8192se_build_wpa2_assoc_request(const uint8_t bssid[6],
                                       const uint8_t station_address[6],
                                       const uint8_t *ssid,
                                       uint8_t ssid_length,
                                       uint16_t sequence_number,
                                       uint8_t *frame, uint32_t capacity,
                                       uint32_t *frame_length)
{
    static const uint8_t supported_rates[8] = {
        0x82u, 0x84u, 0x8bu, 0x96u, 0x0cu, 0x12u, 0x18u, 0x24u,
    };
    static const uint8_t extended_rates[4] = {
        0x30u, 0x48u, 0x60u, 0x6cu,
    };
    static const uint8_t rsn_ie[22] = {
        WLAN_RSN_ELEMENT, 20u, 1u, 0u, 0u, 0x0fu, 0xacu, 4u,
        1u, 0u, 0u, 0x0fu, 0xacu, 4u, 1u, 0u,
        0u, 0x0fu, 0xacu, 2u, 0u, 0u,
    };
    uint32_t required_capacity = WLAN_ASSOC_FIXED_SIZE + 2u + ssid_length +
                                 2u + sizeof(supported_rates) + 2u +
                                 sizeof(extended_rates) + sizeof(rsn_ie);
    if (frame_length == 0 || ssid == 0 || ssid_length == 0u ||
        ssid_length > 32u || capacity < required_capacity ||
        !wlan_write_management_header(frame, capacity, 0x00u, bssid,
                                      station_address, sequence_number)) {
        return 0;
    }
    frame[24] = 0x11u;
    frame[25] = 0x00u;
    frame[26] = 10u;
    frame[27] = 0u;
    uint32_t offset = WLAN_ASSOC_FIXED_SIZE;
    frame[offset++] = WLAN_SSID_ELEMENT;
    frame[offset++] = ssid_length;
    wlan_copy_bytes(frame + offset, ssid, ssid_length);
    offset += ssid_length;
    frame[offset++] = WLAN_SUPPORTED_RATES_ELEMENT;
    frame[offset++] = sizeof(supported_rates);
    wlan_copy_bytes(frame + offset, supported_rates,
                    sizeof(supported_rates));
    offset += sizeof(supported_rates);
    frame[offset++] = WLAN_EXTENDED_RATES_ELEMENT;
    frame[offset++] = sizeof(extended_rates);
    wlan_copy_bytes(frame + offset, extended_rates, sizeof(extended_rates));
    offset += sizeof(extended_rates);
    wlan_copy_bytes(frame + offset, rsn_ie, sizeof(rsn_ie));
    offset += sizeof(rsn_ie);
    *frame_length = offset;
    return 1;
}

int rtl8192se_parse_open_assoc_response(const uint8_t *frame,
                                        uint32_t frame_length,
                                        const uint8_t bssid[6],
                                        const uint8_t station_address[6],
                                        uint8_t *associated)
{
    if (frame == 0 || bssid == 0 || station_address == 0 ||
        associated == 0 ||
        frame_length < WLAN_ASSOC_RESPONSE_FIXED_SIZE ||
        (frame[0] & 0xfcu) != 0x10u || (frame[1] & 0x03u) != 0u ||
        !wlan_bytes_equal(frame + 4u, station_address, 6u) ||
        !wlan_bytes_equal(frame + 10u, bssid, 6u) ||
        !wlan_bytes_equal(frame + 16u, bssid, 6u)) {
        return 0;
    }
    uint32_t offset = WLAN_ASSOC_RESPONSE_FIXED_SIZE;
    while (offset < frame_length) {
        if (frame_length - offset < 2u ||
            frame[offset + 1u] > frame_length - offset - 2u) {
            return 0;
        }
        offset += 2u + frame[offset + 1u];
    }
    uint16_t status = (uint16_t)frame[26] | ((uint16_t)frame[27] << 8);
    uint16_t association_id =
        (uint16_t)frame[28] | ((uint16_t)frame[29] << 8);
    *associated = (uint8_t)(status == 0u &&
                            (association_id & 0x3fffu) != 0u);
    return 1;
}

int rtl8192se_parse_wpa2_assoc_response(const uint8_t *frame,
                                        uint32_t frame_length,
                                        const uint8_t bssid[6],
                                        const uint8_t station_address[6],
                                        uint8_t *associated)
{
    uint8_t accepted = 0u;
    if (!rtl8192se_parse_open_assoc_response(frame, frame_length, bssid,
                                              station_address, &accepted)) {
        return 0;
    }
    if (accepted == 0u) {
        *associated = 0u;
        return 1;
    }

    uint32_t offset = WLAN_ASSOC_RESPONSE_FIXED_SIZE;
    uint8_t found_rsn = 0u;
    uint8_t supports_psk_ccmp = 0u;
    while (offset < frame_length) {
        uint8_t element_id = frame[offset];
        uint8_t element_length = frame[offset + 1u];
        offset += 2u;
        if (element_id == WLAN_RSN_ELEMENT) {
            if (found_rsn != 0u ||
                !wlan_parse_rsn(frame + offset, element_length,
                                &supports_psk_ccmp)) {
                return 0;
            }
            found_rsn = 1u;
        }
        offset += element_length;
    }
    *associated = (uint8_t)(found_rsn != 0u && supports_psk_ccmp != 0u);
    return 1;
}

int rtl8192se_encapsulate_ethernet(const uint8_t *ethernet_frame,
                                   uint32_t ethernet_length,
                                   const uint8_t bssid[6],
                                   const uint8_t station_address[6],
                                   uint16_t sequence_number,
                                   uint8_t *wireless_frame,
                                   uint32_t wireless_capacity,
                                   uint32_t *wireless_length)
{
    if (ethernet_frame == 0 || bssid == 0 || station_address == 0 ||
        wireless_frame == 0 || wireless_length == 0 ||
        ethernet_length < ETHERNET_HEADER_SIZE ||
        ethernet_length > 1536u ||
        sequence_number > 0x0fffu ||
        wireless_capacity < WLAN_DATA_HEADER_SIZE + WLAN_LLC_SNAP_SIZE +
                                ethernet_length - ETHERNET_HEADER_SIZE) {
        return 0;
    }

    uint32_t payload_length = ethernet_length - ETHERNET_HEADER_SIZE;
    wireless_frame[0] = 0x08u;
    wireless_frame[1] = 0x01u;
    wireless_frame[2] = 0u;
    wireless_frame[3] = 0u;
    wlan_copy_bytes(wireless_frame + 4u, bssid, 6u);
    wlan_copy_bytes(wireless_frame + 10u, station_address, 6u);
    wlan_copy_bytes(wireless_frame + 16u, ethernet_frame, 6u);
    wireless_frame[22] = (uint8_t)(sequence_number << 4);
    wireless_frame[23] = (uint8_t)(sequence_number >> 4);
    wireless_frame[24] = 0xaau;
    wireless_frame[25] = 0xaau;
    wireless_frame[26] = 0x03u;
    wireless_frame[27] = 0u;
    wireless_frame[28] = 0u;
    wireless_frame[29] = 0u;
    wireless_frame[30] = ethernet_frame[12u];
    wireless_frame[31] = ethernet_frame[13u];
    wlan_copy_bytes(wireless_frame + 32u, ethernet_frame + ETHERNET_HEADER_SIZE,
                    payload_length);
    *wireless_length = WLAN_DATA_HEADER_SIZE + WLAN_LLC_SNAP_SIZE +
                       payload_length;
    return 1;
}

int rtl8192se_decapsulate_ethernet(const uint8_t *wireless_frame,
                                   uint32_t wireless_length,
                                   const uint8_t bssid[6],
                                   const uint8_t station_address[6],
                                   uint8_t *ethernet_frame,
                                   uint32_t ethernet_capacity,
                                   uint32_t *ethernet_length)
{
    if (wireless_frame == 0 || bssid == 0 || station_address == 0 ||
        ethernet_frame == 0 ||
        ethernet_length == 0 || wireless_length < WLAN_DATA_HEADER_SIZE ||
        (wireless_frame[0] & 0x03u) != 0u ||
        (wireless_frame[0] & 0x0cu) != 0x08u ||
        (wireless_frame[1] & 0x03u) != 0x02u ||
        (wireless_frame[1] & 0x44u) != 0u ||
        (wireless_frame[1] & 0x80u) != 0u ||
        (wireless_frame[22] & 0x0fu) != 0u ||
        !wlan_bytes_equal(wireless_frame + 10u, bssid, 6u) ||
        ((wireless_frame[0] >> 4) & 0x0fu) > 11u ||
        ((wireless_frame[4] & 1u) == 0u &&
         !wlan_bytes_equal(wireless_frame + 4u, station_address, 6u))) {
        return 0;
    }

    uint32_t subtype = (wireless_frame[0] >> 4) & 0x0fu;
    if (subtype >= 4u && subtype <= 7u) {
        return 0;
    }
    uint32_t header_length = WLAN_DATA_HEADER_SIZE;
    if (subtype >= 8u) {
        header_length += 2u;
    }
    if (wireless_length < header_length + WLAN_LLC_SNAP_SIZE) {
        return 0;
    }
    const uint8_t *llc = wireless_frame + header_length;
    if (llc[0] != 0xaau || llc[1] != 0xaau || llc[2] != 0x03u ||
        llc[3] != 0u || llc[4] != 0u || llc[5] != 0u) {
        return 0;
    }

    uint32_t payload_length =
        wireless_length - header_length - WLAN_LLC_SNAP_SIZE;
    uint32_t output_length = ETHERNET_HEADER_SIZE + payload_length;
    if (ethernet_capacity < output_length) {
        return 0;
    }
    wlan_copy_bytes(ethernet_frame, wireless_frame + 4u, 6u);
    wlan_copy_bytes(ethernet_frame + 6u, wireless_frame + 16u, 6u);
    ethernet_frame[12] = llc[6];
    ethernet_frame[13] = llc[7];
    wlan_copy_bytes(ethernet_frame + ETHERNET_HEADER_SIZE,
                    llc + WLAN_LLC_SNAP_SIZE, payload_length);
    *ethernet_length = output_length;
    return 1;
}

int rtl8192se_parse_efuse(const uint8_t *physical_map, uint32_t map_size,
                          struct rtl8192se_adapter_config *config)
{
    if (physical_map == 0 || map_size != EFUSE_PHYSICAL_SIZE ||
        config == 0) {
        return 0;
    }

    uint8_t logical_map[EFUSE_LOGICAL_SIZE];
    for (uint32_t index = 0u; index < EFUSE_LOGICAL_SIZE; ++index) {
        logical_map[index] = 0xffu;
    }

    uint32_t address = 0u;
    while (address < map_size && physical_map[address] != 0xffu) {
        uint8_t header = physical_map[address++];
        uint32_t section;
        uint8_t word_enable;
        if ((header & 0x1fu) == 0x0fu) {
            if (address >= map_size) {
                return 0;
            }
            uint8_t extended = physical_map[address++];
            if (extended == 0xffu) {
                break;
            }
            if ((extended & 0x0fu) == 0x0fu) {
                return 0;
            }
            section = ((uint32_t)(header & 0xe0u) >> 5) |
                      ((extended & 0xf0u) >> 1);
            word_enable = extended & 0x0fu;
        } else {
            section = (header >> 4) & 0x0fu;
            word_enable = header & 0x0fu;
        }

        for (uint32_t word = 0u; word < EFUSE_WORD_COUNT; ++word) {
            if ((word_enable & (1u << word)) != 0u) {
                continue;
            }
            if (address > map_size - 2u) {
                return 0;
            }
            if (section < EFUSE_SECTION_COUNT) {
                uint32_t destination = section * 8u + word * 2u;
                logical_map[destination] = physical_map[address];
                logical_map[destination + 1u] = physical_map[address + 1u];
            }
            address += 2u;
        }
    }

    uint16_t signature =
        (uint16_t)logical_map[EEPROM_SIGNATURE_OFFSET] |
        ((uint16_t)logical_map[EEPROM_SIGNATURE_OFFSET + 1u] << 8);
    if (signature != EEPROM_SIGNATURE) {
        return 0;
    }

    uint8_t board_type = logical_map[EEPROM_BOARD_TYPE];
    uint8_t rf_configuration;
    if (board_type == 0u) {
        rf_configuration = 0x22u;
    } else if (board_type == 1u) {
        rf_configuration = 0x12u;
    } else if (board_type == 2u) {
        rf_configuration = 0x12u;
    } else if (board_type == 3u) {
        rf_configuration = 0x11u;
    } else {
        return 0;
    }
    if (!valid_mac_address(&logical_map[EEPROM_MAC_ADDRESS])) {
        return 0;
    }

    for (uint32_t index = 0u; index < 6u; ++index) {
        config->mac_address[index] = logical_map[EEPROM_MAC_ADDRESS + index];
    }
    config->cck_tx_power_valid_mask = 0u;
    for (uint32_t index = 0u; index < 3u; ++index) {
        config->cck_tx_power[index] =
            logical_map[EEPROM_TX_POWER_BASE + index];
        if (config->cck_tx_power[index] != 0xffu) {
            config->cck_tx_power_valid_mask |= (uint8_t)(1u << index);
        }
    }
    config->rf_configuration = rf_configuration;
    config->channel_plan = logical_map[EEPROM_CHANNEL_PLAN];
    return 1;
}

static int read_efuse_byte(const struct rtl8192se_register_io *io,
                           uint16_t address, uint8_t *value)
{
    if (address >= EFUSE_PHYSICAL_SIZE || value == 0) {
        return 0;
    }
    write8(io, EFUSE_CONTROL + 1u, (uint8_t)address);
    uint8_t high = read8(io, EFUSE_CONTROL + 2u);
    write8(io, EFUSE_CONTROL + 2u,
           (uint8_t)((high & 0xfcu) | ((address >> 8) & 0x03u)));
    write8(io, EFUSE_CONTROL + 3u,
           (uint8_t)(read8(io, EFUSE_CONTROL + 3u) & 0x7fu));

    for (uint32_t attempt = 0u; attempt < 1000u; ++attempt) {
        if ((read8(io, EFUSE_CONTROL + 3u) & 0x80u) != 0u) {
            *value = read8(io, EFUSE_CONTROL);
            return 1;
        }
        io->delay_us(io->context, 1u);
    }
    return 0;
}

int rtl8192se_read_adapter_config(
    const struct rtl8192se_register_io *io,
    struct rtl8192se_adapter_config *config)
{
    if (io == 0 || config == 0 || io->read8 == 0 || io->write8 == 0 ||
        io->delay_us == 0) {
        return 0;
    }

    uint8_t value = read8(io, SYS_FUNC_EN + 1u);
    write8(io, SYS_FUNC_EN + 1u, (uint8_t)(value | 0x20u));
    value = read8(io, SYS_ISO_CTRL + 1u);
    write8(io, SYS_ISO_CTRL + 1u, (uint8_t)(value & 0xfeu));
    value = read8(io, EFUSE_TEST + 3u);
    write8(io, EFUSE_TEST + 3u, (uint8_t)(value | 0x80u));
    write8(io, 0x02f8u, 0x03u);
    write8(io, EFUSE_CONTROL + 3u, 0x72u);

    uint8_t physical_map[EFUSE_PHYSICAL_SIZE];
    for (uint16_t address = 0u; address < EFUSE_PHYSICAL_SIZE; ++address) {
        if (!read_efuse_byte(io, address, &physical_map[address])) {
            return 0;
        }
    }
    return rtl8192se_parse_efuse(physical_map, sizeof(physical_map), config);
}

int rtl8192se_build_rx_ring(uint8_t *ring, uint32_t ring_address,
                            const uint32_t *buffer_addresses,
                            uint32_t descriptor_count,
                            uint32_t buffer_capacity)
{
    if (ring == 0 || buffer_addresses == 0 ||
        descriptor_count < 2u ||
        descriptor_count > RX_DESCRIPTOR_COUNT_MAX ||
        (ring_address & 0xffu) != 0u ||
        ring_address >= RX_DMA_ADDRESS_LIMIT ||
        descriptor_count * RX_DESCRIPTOR_SIZE > 4096u ||
        ring_address > RX_DMA_ADDRESS_LIMIT -
                           descriptor_count * RX_DESCRIPTOR_SIZE ||
        buffer_capacity == 0u || buffer_capacity > RX_BUFFER_MAX_SIZE) {
        return 0;
    }

    for (uint32_t index = 0u; index < descriptor_count; ++index) {
        uint32_t buffer_address = buffer_addresses[index];
        if ((buffer_address & 0xfffu) != 0u ||
            buffer_address >= RX_DMA_ADDRESS_LIMIT ||
            buffer_address > RX_DMA_ADDRESS_LIMIT - buffer_capacity) {
            return 0;
        }
    }

    for (uint32_t index = 0u; index < descriptor_count; ++index) {
        uint8_t *descriptor = ring + index * RX_DESCRIPTOR_SIZE;
        for (uint32_t byte = 0u; byte < RX_DESCRIPTOR_SIZE; ++byte) {
            descriptor[byte] = 0u;
        }
        write_le32(descriptor + RX_DESCRIPTOR_BUFFER_ADDRESS_OFFSET,
                   buffer_addresses[index]);
        uint32_t control = buffer_capacity;
        if (index + 1u == descriptor_count) {
            control |= RX_DESCRIPTOR_END_OF_RING;
        }
        __asm__ volatile ("" : : : "memory");
        write_le32(descriptor, control | RX_DESCRIPTOR_OWN);
    }
    return 1;
}

int rtl8192se_rearm_rx_descriptor(uint8_t *descriptor,
                                  uint32_t buffer_address,
                                  uint32_t buffer_capacity,
                                  int end_of_ring)
{
    if (descriptor == 0 || buffer_capacity == 0u ||
        buffer_capacity > RX_BUFFER_MAX_SIZE ||
        (buffer_address & 0xfffu) != 0u ||
        buffer_address >= RX_DMA_ADDRESS_LIMIT ||
        buffer_address > RX_DMA_ADDRESS_LIMIT - buffer_capacity) {
        return 0;
    }

    for (uint32_t byte = 0u; byte < RX_DESCRIPTOR_SIZE; ++byte) {
        descriptor[byte] = 0u;
    }
    write_le32(descriptor + RX_DESCRIPTOR_BUFFER_ADDRESS_OFFSET,
               buffer_address);
    uint32_t control = buffer_capacity;
    if (end_of_ring != 0) {
        control |= RX_DESCRIPTOR_END_OF_RING;
    }
    __asm__ volatile ("" : : : "memory");
    write_le32(descriptor, control | RX_DESCRIPTOR_OWN);
    return 1;
}

int rtl8192se_program_rx_rings(const struct rtl8192se_register_io *io,
                               uint32_t mpdu_ring_address,
                               uint32_t command_ring_address)
{
    if (io == 0 || io->write32 == 0 || io->read32 == 0 ||
        (mpdu_ring_address & 0xffu) != 0u ||
        (command_ring_address & 0xffu) != 0u ||
        mpdu_ring_address == 0u || command_ring_address == 0u ||
        mpdu_ring_address >= RX_DMA_ADDRESS_LIMIT ||
        command_ring_address >= RX_DMA_ADDRESS_LIMIT) {
        return 0;
    }

    write32(io, RX_MPDU_RING_ADDRESS, mpdu_ring_address);
    write32(io, RX_COMMAND_RING_ADDRESS, command_ring_address);
    return read32(io, RX_MPDU_RING_ADDRESS) == mpdu_ring_address &&
           read32(io, RX_COMMAND_RING_ADDRESS) == command_ring_address;
}

int rtl8192se_build_tx_descriptor(uint8_t *descriptor,
                                  uint32_t buffer_address,
                                  uint32_t frame_length,
                                  uint32_t next_descriptor_address,
                                  uint8_t queue_selection,
                                  uint8_t non_qos)
{
    if (descriptor == 0 || buffer_address == 0u ||
        buffer_address >= RX_DMA_ADDRESS_LIMIT ||
        frame_length < 10u || frame_length > 4096u ||
        next_descriptor_address == 0u ||
        (next_descriptor_address & 0x3fu) != 0u ||
        next_descriptor_address >= RX_DMA_ADDRESS_LIMIT ||
        queue_selection > 0x1fu || non_qos > 1u) {
        return 0;
    }

    for (uint32_t index = 0u; index < 64u; ++index) {
        descriptor[index] = 0u;
    }
    write_le32(descriptor + 4u,
               ((uint32_t)queue_selection << 8) |
                   ((uint32_t)non_qos << 16));
    write_le32(descriptor + 16u, 1u << 31);
    write_le32(descriptor + 28u, frame_length);
    write_le32(descriptor + 32u, buffer_address);
    write_le32(descriptor + 36u, next_descriptor_address);
    __asm__ volatile ("" : : : "memory");
    uint32_t flags = frame_length | (32u << 16) | (1u << 26) |
                     (1u << 27) | (1u << 31);
    write_le32(descriptor, flags);
    return 1;
}

int rtl8192se_program_tx_rings(const struct rtl8192se_register_io *io,
                               uint32_t data_ring_address,
                               uint32_t management_ring_address)
{
    if (io == 0 || io->write32 == 0 || io->read32 == 0 ||
        (data_ring_address & 0xffu) != 0u ||
        (management_ring_address & 0xffu) != 0u ||
        data_ring_address == 0u || management_ring_address == 0u ||
        data_ring_address >= RX_DMA_ADDRESS_LIMIT ||
        management_ring_address >= RX_DMA_ADDRESS_LIMIT) {
        return 0;
    }

    write32(io, TX_DATA_RING_ADDRESS, data_ring_address);
    write32(io, TX_MANAGEMENT_RING_ADDRESS, management_ring_address);
    return read32(io, TX_DATA_RING_ADDRESS) == data_ring_address &&
           read32(io, TX_MANAGEMENT_RING_ADDRESS) ==
               management_ring_address;
}

static uint32_t rtl8192se_mask_shift(uint32_t mask)
{
    uint32_t shift = 0u;
    while (mask != 0u && (mask & 1u) == 0u) {
        mask >>= 1;
        ++shift;
    }
    return shift;
}

static void rtl8192se_write_bb_field(const struct rtl8192se_register_io *io,
                                     uint16_t address, uint32_t mask,
                                     uint32_t value)
{
    uint32_t original = read32(io, address);
    uint32_t shift = rtl8192se_mask_shift(mask);
    write32(io, address, (original & ~mask) | ((value << shift) & mask));
}

static void rtl8192se_delay_table_address(
    const struct rtl8192se_register_io *io, uint32_t address)
{
    if (address == 0xfeu) {
        io->delay_us(io->context, 50000u);
    } else if (address == 0xfdu) {
        io->delay_us(io->context, 5000u);
    } else if (address == 0xfcu) {
        io->delay_us(io->context, 1000u);
    } else if (address == 0xfbu) {
        io->delay_us(io->context, 50u);
    } else if (address == 0xfau) {
        io->delay_us(io->context, 5u);
    } else if (address == 0xf9u) {
        io->delay_us(io->context, 1u);
    }
}

static void rtl8192se_write_rf_register(
    const struct rtl8192se_register_io *io, uint8_t path, uint8_t address,
    uint32_t value)
{
    uint16_t lssi_address =
        path == 0u ? BB_RF_PATH_A_LSSI : BB_RF_PATH_B_LSSI;
    write32(io, lssi_address,
            (((uint32_t)address << 20) | value) & 0x0fffffffu);
    io->delay_us(io->context, 1u);
    if (address == RF_REGISTER_CHANNEL_BANDWIDTH) {
        rf_channel_bandwidth[path] = value & RF_REGISTER_MASK;
    }
}

static uint32_t rtl8192se_read_rf_register(
    const struct rtl8192se_register_io *io, uint8_t path, uint8_t address)
{
    uint16_t path_hssi =
        path == 0u ? BB_RF_PATH_A_HSSI2 : BB_RF_PATH_B_HSSI2;
    uint32_t path_parameter = read32(io, path_hssi);
    uint32_t common_parameter = read32(io, BB_RF_PATH_A_HSSI2);
    path_parameter = (path_parameter & ~RF_READ_ADDRESS_MASK) |
                     ((uint32_t)(address & 0x3fu) << 23) | RF_READ_EDGE;
    write32(io, BB_RF_PATH_A_HSSI2, common_parameter & ~RF_READ_EDGE);
    io->delay_us(io->context, 1000u);
    write32(io, path_hssi, path_parameter);
    io->delay_us(io->context, 1000u);
    write32(io, BB_RF_PATH_A_HSSI2,
            common_parameter | RF_READ_EDGE);
    io->delay_us(io->context, 1000u);

    uint16_t hssi1 = path == 0u ? RF_HSSI_PATH_A : RF_HSSI_PATH_B;
    uint16_t readback =
        (read32(io, hssi1) & (1u << 8)) != 0u
            ? (path == 0u ? RF_READBACK_PATH_A_PI : RF_READBACK_PATH_B_PI)
            : (path == 0u ? RF_READBACK_PATH_A : RF_READBACK_PATH_B);
    return read32(io, readback) & RF_READBACK_MASK;
}

static int rtl8192se_configure_rf_path(
    const struct rtl8192se_register_io *io, uint8_t path,
    const uint32_t *table, uint32_t table_length)
{
    uint16_t interface_address =
        path == 0u ? BB_RF_PATH_A_INTERFACE : BB_RF_PATH_B_INTERFACE;
    uint16_t hssi2_address =
        path == 0u ? BB_RF_PATH_A_HSSI2 : BB_RF_PATH_B_HSSI2;
    uint32_t switch_mask =
        path == 0u ? RF_INTERFACE_ENABLE_BIT : RF_INTERFACE_ENABLE_MASK;
    uint32_t old_switch =
        (read32(io, BB_RF_INTERFACE_SWITCH) & switch_mask) >>
        rtl8192se_mask_shift(switch_mask);

    rtl8192se_write_bb_field(io, interface_address,
                             RF_INTERFACE_ENABLE_MASK, 1u);
    rtl8192se_write_bb_field(io, interface_address,
                             RF_INTERFACE_ENABLE_BIT, 1u);
    rtl8192se_write_bb_field(io, hssi2_address,
                             RF_HSSI_ADDRESS_LENGTH, 0u);
    rtl8192se_write_bb_field(io, hssi2_address,
                             RF_HSSI_DATA_LENGTH, 0u);

    for (uint32_t index = 0u; index < table_length; index += 2u) {
        uint32_t address = table[index];
        if (address >= 0xf9u && address <= 0xfeu) {
            rtl8192se_delay_table_address(io, address);
        } else {
            rtl8192se_write_rf_register(
                io, path, (uint8_t)address, table[index + 1u]);
        }
    }

    rtl8192se_write_bb_field(io, BB_RF_INTERFACE_SWITCH, switch_mask,
                             old_switch);
    return 1;
}

int rtl8192se_set_channel(const struct rtl8192se_register_io *io,
                          uint8_t channel)
{
    if (io == 0 || io->write8 == 0 || io->write32 == 0 ||
        io->delay_us == 0 || phy_initialized == 0u ||
        channel == 0u || channel > 13u || rf_path_count == 0u) {
        return 0;
    }
    uint8_t power_group = channel <= 3u ? 0u : (channel <= 8u ? 1u : 2u);
    if ((cck_tx_power_valid_mask & (1u << power_group)) != 0u) {
        uint8_t power = cck_tx_power[power_group];
        if (power > 0x3fu) {
            power = 0x3fu;
        }
        rtl8192se_write_bb_field(io, BB_TX_POWER_CCK,
                                 BB_TX_POWER_CCK_MASK, power);
    }
    for (uint8_t path = 0u; path < rf_path_count; ++path) {
        rf_channel_bandwidth[path] =
            (rf_channel_bandwidth[path] & ~RF_CHANNEL_MASK) | channel;
        rtl8192se_write_rf_register(
            io, path, RF_REGISTER_CHANNEL_BANDWIDTH,
            rf_channel_bandwidth[path]);
    }
    io->write8(io->context, MAC_BANDWIDTH_OPERATION,
               (uint8_t)(io->read8(io->context, MAC_BANDWIDTH_OPERATION) |
                         (1u << 2)));
    rtl8192se_write_bb_field(io, BB_RF_MODE_0, 0x1u, 0u);
    rtl8192se_write_bb_field(io, BB_RF_MODE_1, 0x1u, 0u);
    io->delay_us(io->context, 10000u);
    return 1;
}

int rtl8192se_initialize_phy(
    const struct rtl8192se_register_io *io,
    const struct rtl8192se_adapter_config *config)
{
    if (io == 0 || config == 0 || io->read8 == 0 || io->read32 == 0 ||
        io->write8 == 0 || io->write32 == 0 || io->delay_us == 0 ||
        !valid_mac_address(config->mac_address)) {
        return 0;
    }
    phy_initialized = 0u;
    rf_channel_bandwidth[0] = 0u;
    rf_channel_bandwidth[1] = 0u;
    cck_tx_power_valid_mask = config->cck_tx_power_valid_mask & 0x07u;
    for (uint32_t index = 0u; index < 3u; ++index) {
        cck_tx_power[index] = config->cck_tx_power[index];
    }

    uint8_t tx_paths = config->rf_configuration >> 4;
    uint8_t rx_paths = config->rf_configuration & 0x0fu;
    if (!((tx_paths == 1u && (rx_paths == 1u || rx_paths == 2u)) ||
          (tx_paths == 2u && rx_paths == 2u))) {
        return 0;
    }

    for (uint32_t index = 0u; index < MAC_2T_ARRAYLENGTH; index += 2u) {
        io->write8(io->context, (uint16_t)rtl8192semac_2t_array[index],
                   (uint8_t)rtl8192semac_2t_array[index + 1u]);
    }
    for (uint32_t index = 0u; index < PHY_REG_2T2RARRAYLENGTH; index += 2u) {
        rtl8192se_delay_table_address(io,
                                      rtl8192sephy_reg_2t2rarray[index]);
        io->write32(io->context,
                    (uint16_t)rtl8192sephy_reg_2t2rarray[index],
                    rtl8192sephy_reg_2t2rarray[index + 1u]);
        io->delay_us(io->context, 1u);
    }

    const uint32_t *rf_layout = 0;
    uint32_t rf_layout_length = 0u;
    if (tx_paths == 1u && rx_paths == 1u) {
        rf_layout = rtl8192sephy_changeto_1t1rarray;
        rf_layout_length = PHY_CHANGETO_1T1RARRAYLENGTH;
    } else if (tx_paths == 1u && rx_paths == 2u) {
        rf_layout = rtl8192sephy_changeto_1t2rarray;
        rf_layout_length = PHY_CHANGETO_1T2RARRAYLENGTH;
    }
    if (rf_layout != 0) {
        for (uint32_t index = 0u; index < rf_layout_length; index += 3u) {
            rtl8192se_delay_table_address(io, rf_layout[index]);
            rtl8192se_write_bb_field(
                io, (uint16_t)rf_layout[index], rf_layout[index + 1u],
                rf_layout[index + 2u]);
        }
    }

    for (uint32_t index = 0u; index < PHY_REG_ARRAY_PGLENGTH; index += 3u) {
        rtl8192se_delay_table_address(io, rtl8192sephy_reg_array_pg[index]);
        rtl8192se_write_bb_field(
            io, (uint16_t)rtl8192sephy_reg_array_pg[index],
            rtl8192sephy_reg_array_pg[index + 1u],
            rtl8192sephy_reg_array_pg[index + 2u]);
    }
    for (uint32_t index = 0u; index < AGCTAB_ARRAYLENGTH; index += 2u) {
        io->write32(io->context,
                    (uint16_t)rtl8192seagctab_array[index],
                    rtl8192seagctab_array[index + 1u]);
        io->delay_us(io->context, 1u);
    }

    uint32_t path_map =
        (read32(io, 0x804u) | read32(io, BB_RX_PATH_ENABLE)) & 0x0fu;
    uint8_t enabled_paths = 0u;
    for (uint8_t path = 0u; path < 4u; ++path) {
        enabled_paths += (uint8_t)((path_map >> path) & 1u);
    }
    rf_path_count = (uint8_t)(tx_paths == 1u && rx_paths == 1u ? 1u : 2u);
    if (enabled_paths != rf_path_count) {
        rf_path_count = 0u;
        return 0;
    }

    io->write8(io->context, 0x0027u, 0xdbu);
    io->write8(io->context, MAC_FUNCTION_CONTROL, 0x07u);
    if (!rtl8192se_configure_rf_path(
            io, 0u, rtl8192seradioa_1t_array,
            RADIOA_1T_ARRAYLENGTH)) {
        rf_path_count = 0u;
        return 0;
    }
    if (rf_path_count > 1u &&
        !rtl8192se_configure_rf_path(io, 1u, rtl8192seradiob_array,
                                     RADIOB_ARRAYLENGTH)) {
        rf_path_count = 0u;
        return 0;
    }

    rf_channel_bandwidth[0] = rtl8192se_read_rf_register(
        io, 0u, RF_REGISTER_CHANNEL_BANDWIDTH);
    if (rf_channel_bandwidth[0] == 0u) {
        rf_channel_bandwidth[0] = 0x0000f401u;
    }
    if (rf_path_count > 1u) {
        rf_channel_bandwidth[1] = rtl8192se_read_rf_register(
            io, 1u, RF_REGISTER_CHANNEL_BANDWIDTH);
        if (rf_channel_bandwidth[1] == 0u) {
            rf_channel_bandwidth[1] = rf_channel_bandwidth[0];
        }
    }
    for (uint8_t path = 0u; path < rf_path_count; ++path) {
        rf_channel_bandwidth[path] =
            (rf_channel_bandwidth[path] & 0xfffff3ffu) |
            RF_BANDWIDTH_20MHZ;
        rtl8192se_write_rf_register(
            io, path, RF_REGISTER_CHANNEL_BANDWIDTH,
            rf_channel_bandwidth[path]);
    }
    rtl8192se_write_bb_field(io, BB_RF_MODE_0, 0x01000000u, 1u);
    rtl8192se_write_bb_field(io, BB_RF_MODE_0, 0x02000000u, 1u);
    rtl8192se_write_bb_field(io, BB_RF_MODE_1, 0x01000000u, 1u);
    rtl8192se_write_bb_field(io, BB_RF_MODE_1, 0x02000000u, 1u);
    for (uint32_t index = 0u; index < 6u; ++index) {
        io->write8(io->context, (uint16_t)(MAC_ADDRESS_REGISTER + index),
                   config->mac_address[index]);
    }

    phy_initialized = 1u;
    if (!rtl8192se_set_channel(io, 1u)) {
        phy_initialized = 0u;
        return 0;
    }
    return 1;
}

int rtl8192se_enable_mac_io(const struct rtl8192se_register_io *io)
{
    if (io == 0 || io->read8 == 0 || io->read16 == 0 ||
        io->read32 == 0 || io->write8 == 0 || io->write16 == 0 ||
        io->write32 == 0) {
        return 0;
    }

    uint32_t receive_config = RECEIVE_ALL;
    write32(io, RECEIVE_CONFIG, receive_config);
    write8(io, RX_DRIVER_INFO_SIZE, 4u);
    write8(io, RXDMA,
           (uint8_t)(read8(io, RXDMA) | RXDMA_ARBITRATION));
    write16(io, COMMAND, MAC_IO_ENABLE);

    return (read32(io, RECEIVE_CONFIG) & RECEIVE_ALL) == RECEIVE_ALL &&
           read8(io, RX_DRIVER_INFO_SIZE) == 4u &&
           (read8(io, RXDMA) & RXDMA_ARBITRATION) != 0u &&
           (read16(io, COMMAND) & MAC_IO_ENABLE_MASK) == MAC_IO_ENABLE;
}

int rtl8192se_rx_copy_frame(const uint8_t *descriptor,
                            const uint8_t *buffer,
                            uint32_t buffer_capacity, uint8_t *frame,
                            uint32_t frame_capacity, uint32_t *frame_length)
{
    if (descriptor == 0 || buffer == 0 || frame == 0 ||
        frame_length == 0) {
        return 0;
    }
    uint32_t control = (uint32_t)descriptor[0] |
                      ((uint32_t)descriptor[1] << 8) |
                      ((uint32_t)descriptor[2] << 16) |
                      ((uint32_t)descriptor[3] << 24);
    uint32_t length = control & RX_DESCRIPTOR_LENGTH_MASK;
    uint32_t info_length =
        ((control & RX_DESCRIPTOR_INFO_LENGTH_MASK) >> 16) * 8u;
    uint32_t buffer_shift =
        (control & RX_DESCRIPTOR_BUFFER_SHIFT_MASK) >> 24;
    uint32_t frame_offset = info_length + buffer_shift;
    if ((control & RX_DESCRIPTOR_OWN) != 0u ||
        (control & (RX_DESCRIPTOR_CRC_ERROR | RX_DESCRIPTOR_ICV_ERROR)) !=
            0u ||
        length <= RX_FRAME_FCS_LENGTH || frame_offset > buffer_capacity ||
        length > buffer_capacity - frame_offset ||
        length - RX_FRAME_FCS_LENGTH > frame_capacity) {
        return 0;
    }

    uint32_t payload_length = length - RX_FRAME_FCS_LENGTH;
    for (uint32_t index = 0u; index < payload_length; ++index) {
        frame[index] = buffer[frame_offset + index];
    }
    *frame_length = payload_length;
    return 1;
}

static int switch_to_mac_clock(const struct rtl8192se_register_io *io,
                               uint8_t value)
{
    write8(io, SYS_CLKR + 1u, value);
    io->delay_us(io->context, 400u);
    if ((read8(io, SYS_CLKR + 1u) & 0x80u) != (value & 0x80u)) {
        return 0;
    }

    if ((value & 0xc0u) == 0u) {
        for (uint32_t attempt = 0u; attempt < 100u; ++attempt) {
            if ((read8(io, SYS_CLKR + 1u) & 0x40u) != 0u) {
                return 1;
            }
            io->delay_us(io->context, 10u);
        }
        return 0;
    }
    return 1;
}

int rtl8192se_mac_power_sequence(const struct rtl8192se_register_io *io,
                                 uint32_t command_ring_address,
                                 uint32_t mpdu_ring_address,
                                 uint32_t rx_command_ring_address,
                                 uint32_t data_ring_address,
                                 uint32_t management_ring_address)
{
    if (io == 0 || io->read8 == 0 || io->read16 == 0 || io->read32 == 0 ||
        io->write8 == 0 || io->write16 == 0 || io->write32 == 0 ||
        io->delay_us == 0 || command_ring_address == 0u ||
        (command_ring_address & 0xffu) != 0u ||
        command_ring_address >= DMA_ADDRESS_LIMIT ||
        command_ring_address > DMA_ADDRESS_LIMIT -
            COMMAND_QUEUE_COUNT * COMMAND_DESCRIPTOR_SIZE) {
        return 0;
    }

    uint8_t value8 = read8(io, SYS_FUNC_EN + 1u);
    write8(io, SYS_FUNC_EN + 1u, (uint8_t)(value8 & 0xfeu));
    io->delay_us(io->context, 1u);
    write8(io, SYS_FUNC_EN + 1u, (uint8_t)(value8 | 0x01u));

    value8 = read8(io, SYS_CLKR + 1u);
    if ((value8 & 0x80u) != 0u &&
        !switch_to_mac_clock(io, (uint8_t)(value8 & 0x3fu))) {
        return 0;
    }

    write8(io, AFE_PLL_CTRL, 0u);
    io->delay_us(io->context, 50u);
    write8(io, LDOA15_CTRL, 0x34u);
    io->delay_us(io->context, 50u);
    write8(io, RF_POWER_CONTROL, 0u);

    value8 = (uint8_t)(read8(io, SYS_FUNC_EN + 1u) & 0x73u);
    write8(io, SYS_FUNC_EN + 1u, value8);
    io->delay_us(io->context, 1000u);
    write16(io, COMMAND, 0u);
    write32(io, TCR, 0u);

    value8 = (uint8_t)(read8(io, 0x0562u) | 0x08u);
    write8(io, 0x0562u, value8);
    write8(io, 0x0562u, (uint8_t)(value8 & 0xf7u));

    write8(io, AFE_XTAL_CTRL,
           (uint8_t)(read8(io, AFE_XTAL_CTRL) | 0x01u));
    io->delay_us(io->context, 2000u);
    write8(io, AFE_XTAL_CTRL + 1u,
           (uint8_t)(read8(io, AFE_XTAL_CTRL + 1u) & 0xfbu));

    write8(io, AFE_MISC, (uint8_t)(read8(io, AFE_MISC) | 0x01u));
    io->delay_us(io->context, 1000u);
    write8(io, AFE_MISC, (uint8_t)(read8(io, AFE_MISC) | 0x02u));
    io->delay_us(io->context, 1000u);
    write8(io, LDOA15_CTRL,
           (uint8_t)(read8(io, LDOA15_CTRL) | 0x01u));

    write16(io, SYS_ISO_CTRL,
            (uint16_t)(read16(io, SYS_ISO_CTRL) | (1u << 11)));
    write16(io, SYS_FUNC_EN,
            (uint16_t)(read16(io, SYS_FUNC_EN) | (1u << 13)));
    write8(io, SYS_ISO_CTRL + 1u, 0x68u);

    io->delay_us(io->context, 200u);
    value8 = read8(io, AFE_PLL_CTRL);
    write8(io, AFE_PLL_CTRL, (uint8_t)(value8 | 0x11u));
    io->delay_us(io->context, 100u);
    write8(io, AFE_PLL_CTRL, (uint8_t)(value8 | 0x51u));
    io->delay_us(io->context, 10u);
    write8(io, AFE_PLL_CTRL, (uint8_t)(value8 | 0x11u));
    io->delay_us(io->context, 10u);
    write8(io, AFE_PLL_CTRL + 1u,
           (uint8_t)(read8(io, AFE_PLL_CTRL + 1u) | 0x01u));
    io->delay_us(io->context, 1000u);

    write8(io, SYS_ISO_CTRL, 0xa6u);
    write16(io, SYS_CLKR,
            (uint16_t)(read16(io, SYS_CLKR) | (1u << 12) | (1u << 11)));
    write16(io, SYS_FUNC_EN,
            (uint16_t)(read16(io, SYS_FUNC_EN) | (1u << 11)));
    write8(io, SYS_FUNC_EN + 1u,
           (uint8_t)(read8(io, SYS_FUNC_EN + 1u) & 0x7fu));
    write16(io, SYS_FUNC_EN,
            (uint16_t)(read16(io, SYS_FUNC_EN) | (1u << 11) | (1u << 15)));
    write16(io, SYS_CLKR, (uint16_t)(read16(io, SYS_CLKR) & ~(1u << 2)));

    value8 = (uint8_t)((read8(io, SYS_CLKR + 1u) | 0x80u) & 0xbfu);
    if (!switch_to_mac_clock(io, value8)) {
        return 0;
    }

    write16(io, COMMAND, COMMAND_DMA_PREPARE);
    write8(io, 0x0006u, 0x30u);
    write8(io, 0x0049u, 0xf0u);
    write8(io, 0x004bu, 0x81u);
    write8(io, 0x00b5u, 0x21u);
    for (uint16_t offset = TX_DESCRIPTOR_MASK;
         offset < TX_DESCRIPTOR_MASK + 4u; ++offset) {
        write8(io, offset, 0xffu);
    }
    write8(io, 0x011au, 0u);
    write8(io, 0x011bu, 0u);
    for (uint16_t offset = INITIAL_RATE_SELECTION;
         offset < INITIAL_RATE_SELECTION + 32u; ++offset) {
        write8(io, offset, 0x1bu);
    }
    write8(io, 0x0236u, 0xffu);
    write32(io, RQPN, 0xffffffffu);
    write32(io, RQPN + 4u, 0xffffffffu);
    write8(io, RQPN + 8u, 0xffu);
    write8(io, QUEUE_PAGE_NUMBER + 11u, 0x80u);
    write8(io, PCI_FUNCTION_CONTROL, 0x22u);
    write8(io, DEBUG_PORT, 0x91u);

    if (!rtl8192se_program_rx_rings(io, mpdu_ring_address,
                                   rx_command_ring_address) ||
        !rtl8192se_program_tx_rings(io, data_ring_address,
                                   management_ring_address)) {
        return 0;
    }
    write32(io, TX_COMMAND_DESCRIPTOR, command_ring_address);
    write16(io, COMMAND, COMMAND_DMA_ENABLE);
    for (uint32_t attempt = 0u; attempt < 1000u; ++attempt) {
        if ((read32(io, TCR) & TCR_DMA_INIT_VALUE) ==
            TCR_DMA_INIT_VALUE) {
            return 1;
        }
        io->delay_us(io->context, 5u);
    }
    return 0;
}

static int wait_for_tcr(const struct rtl8192se_register_io *io,
                        uint32_t mask, uint32_t expected,
                        uint32_t attempts, uint32_t delay_us,
                        uint32_t *observed_tcr)
{
    for (uint32_t attempt = 0u; attempt < attempts; ++attempt) {
        uint32_t value = read32(io, TCR);
        if (observed_tcr != 0) {
            *observed_tcr = value;
        }
        if ((value & mask) == expected) {
            return 1;
        }
        io->delay_us(io->context, delay_us);
    }
    return 0;
}

static int download_segment(const struct rtl8192se_register_io *io,
                            const uint8_t *source, uint32_t size,
                            uint8_t *ring, uint32_t ring_address,
                            uint32_t ring_count, uint8_t *buffer,
                            uint32_t buffer_address, uint32_t capacity,
                            uint32_t *descriptor_index, int *bus_master_enabled)
{
    uint32_t offset = 0u;
    while (offset < size) {
        uint32_t length = size - offset;
        if (length > capacity) {
            length = capacity;
        }
        uint8_t *descriptor =
            ring + *descriptor_index * COMMAND_DESCRIPTOR_SIZE;
        for (uint32_t index = 0u; index < length; ++index) {
            buffer[index] = source[offset + index];
        }

        uint32_t next_index = (*descriptor_index + 1u) % ring_count;
        uint32_t next_address =
            ring_address + next_index * COMMAND_DESCRIPTOR_SIZE;
        if (!rtl8192se_firmware_build_download_descriptor(
                descriptor, buffer_address, length, next_address,
                (uint8_t)(offset + length == size))) {
            return 0;
        }
        __asm__ volatile ("" : : : "memory");
        if (!*bus_master_enabled) {
            if (io->enable_bus_mastering == 0 ||
                !io->enable_bus_mastering(io->bus_context)) {
                return 0;
            }
            *bus_master_enabled = 1;
        }

        write16(io, TX_POLL, COMMAND_QUEUE_POLL);
        uint32_t attempts = 0u;
        while ((read_le32(descriptor) & 0x80000000u) != 0u &&
               attempts < 1000u) {
            io->delay_us(io->context, 5u);
            ++attempts;
        }
        if ((read_le32(descriptor) & 0x80000000u) != 0u) {
            return 0;
        }

        offset += length;
        *descriptor_index = next_index;
    }
    return 1;
}

int rtl8192se_upload_firmware(const struct rtl8192se_register_io *io,
                              const struct rtl8192se_firmware *firmware,
                              uint8_t *ring, uint32_t ring_address,
                              uint32_t ring_count, uint8_t *buffer,
                              uint32_t buffer_address,
                              uint32_t buffer_capacity,
                              uint8_t rf_configuration,
                              enum rtl8192se_firmware_upload_failure *failure,
                              uint32_t *observed_tcr)
{
    if (failure != 0) {
        *failure = RTL8192SE_FW_UPLOAD_INVALID_INPUT;
    }
    if (observed_tcr != 0) {
        *observed_tcr = 0u;
    }
    if (io == 0 || io->read16 == 0 || io->read32 == 0 ||
        io->write16 == 0 || io->delay_us == 0 ||
        io->enable_bus_mastering == 0 || firmware == 0 ||
        firmware->bytes == 0 ||
        ring == 0 || buffer == 0 || ring_count != COMMAND_QUEUE_COUNT ||
        (ring_address & 0xffu) != 0u ||
        ring_address > DMA_ADDRESS_LIMIT -
            ring_count * COMMAND_DESCRIPTOR_SIZE ||
        buffer_address == 0u || buffer_address >= DMA_ADDRESS_LIMIT ||
        buffer_capacity == 0u || buffer_capacity > DMA_CHUNK_SIZE ||
        buffer_address > DMA_ADDRESS_LIMIT - buffer_capacity ||
        firmware->imem_size == 0u || firmware->emem_size == 0u ||
        firmware->imem_offset > firmware->size ||
        firmware->imem_size > firmware->size - firmware->imem_offset ||
        firmware->emem_offset > firmware->size ||
        firmware->emem_size > firmware->size - firmware->emem_offset ||
        firmware->dmem_size != sizeof(uint8_t) * 48u ||
        firmware->dmem_offset > firmware->size ||
        firmware->dmem_size > firmware->size - firmware->dmem_offset) {
        return 0;
    }

    if (observed_tcr != 0) {
        *observed_tcr = read32(io, TCR);
    }
    if (rf_configuration != 0x11u && rf_configuration != 0x12u &&
        rf_configuration != 0x22u) {
        return 0;
    }
    uint8_t private_data[48];
    if (!rtl8192se_firmware_prepare_dmem(firmware, rf_configuration,
                                         private_data,
                                         sizeof(private_data))) {
        return 0;
    }

    uint32_t descriptor_index = 0u;
    int bus_master_enabled = 0;
    if (failure != 0) {
        *failure = RTL8192SE_FW_UPLOAD_IMEM_TRANSFER;
    }
    if (!download_segment(io, &firmware->bytes[firmware->imem_offset],
                          firmware->imem_size, ring, ring_address,
                          ring_count, buffer, buffer_address, buffer_capacity,
                          &descriptor_index, &bus_master_enabled)) {
        if (observed_tcr != 0) {
            *observed_tcr = read32(io, TCR);
        }
        return 0;
    }
    if (failure != 0) {
        *failure = RTL8192SE_FW_UPLOAD_IMEM_CHECK;
    }
    if (!wait_for_tcr(io, IMEM_CODE_DONE | IMEM_CHECK_REPORT,
                      IMEM_CODE_DONE | IMEM_CHECK_REPORT, 1000u, 5u,
                      observed_tcr)) {
        return 0;
    }

    if (failure != 0) {
        *failure = RTL8192SE_FW_UPLOAD_EMEM_TRANSFER;
    }
    if (!download_segment(io, &firmware->bytes[firmware->emem_offset],
                          firmware->emem_size, ring, ring_address,
                          ring_count, buffer, buffer_address, buffer_capacity,
                          &descriptor_index, &bus_master_enabled)) {
        if (observed_tcr != 0) {
            *observed_tcr = read32(io, TCR);
        }
        return 0;
    }
    if (failure != 0) {
        *failure = RTL8192SE_FW_UPLOAD_EMEM_CHECK;
    }
    if (!wait_for_tcr(io, EMEM_CODE_DONE | EMEM_CHECK_REPORT,
                      EMEM_CODE_DONE | EMEM_CHECK_REPORT, 1000u, 5u,
                      observed_tcr)) {
        return 0;
    }

    write16(io, SYS_CLKR,
            (uint16_t)(read16(io, SYS_CLKR) | SYS_CPU_CLKSEL));
    write16(io, SYS_FUNC_EN,
            (uint16_t)(read16(io, SYS_FUNC_EN) | FEN_CPUEN));
    if (failure != 0) {
        *failure = RTL8192SE_FW_UPLOAD_CPU_READY;
    }
    if (!wait_for_tcr(io, IMEM_RDY, IMEM_RDY, 1000u, 100u,
                      observed_tcr)) {
        return 0;
    }

    if (failure != 0) {
        *failure = RTL8192SE_FW_UPLOAD_DMEM_TRANSFER;
    }
    if (!download_segment(io, private_data, sizeof(private_data), ring,
                          ring_address, ring_count, buffer, buffer_address,
                          buffer_capacity, &descriptor_index,
                          &bus_master_enabled)) {
        if (observed_tcr != 0) {
            *observed_tcr = read32(io, TCR);
        }
        return 0;
    }
    if (failure != 0) {
        *failure = RTL8192SE_FW_UPLOAD_DMEM_CHECK;
    }
    if (!wait_for_tcr(io, DMEM_CODE_DONE, DMEM_CODE_DONE, 1000u, 5u,
                      observed_tcr)) {
        return 0;
    }
    if (failure != 0) {
        *failure = RTL8192SE_FW_UPLOAD_FIRMWARE_READY;
    }
    if (!wait_for_tcr(io, FIRMWARE_READY, FIRMWARE_READY, 2000u, 40u,
                      observed_tcr)) {
        return 0;
    }
    if (failure != 0) {
        *failure = RTL8192SE_FW_UPLOAD_OK;
    }
    return 1;
}
