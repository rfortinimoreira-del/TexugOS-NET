#include <stdint.h>
#include <stdio.h>

#include "frame_allocator.h"
#include "paging.h"
#include "pci.h"
#include "rtl8192se.h"
#include "rtl8192se_firmware.h"
#include "rtl8192se_hw.h"

static struct pci_device test_device;
static struct pci_bar test_bar;
static uint32_t mapped_address;
static uint32_t mapped_size;
static uint32_t mock_pit_ticks;
static int map_succeeds = 1;
static uint8_t mock_registers[0x4000];
static uint8_t mock_efuse[512];
static int mock_efuse_ready = 1;
static uint32_t delay_call_count;
static int fail_clock_switch;
static uint32_t mock_tcr;
static uint32_t mock_poll_count;
static int suppress_dma_completion;
static int bus_master_enabled;
static uint8_t test_ring[16u * 64u] __attribute__((aligned(256)));
static uint8_t *test_dma_buffer;

static uint32_t read_le32(const uint8_t *bytes);

uint32_t frame_allocate_below(uint64_t address_limit)
{
    (void)address_limit;
    return FRAME_INVALID;
}

uint32_t pit_ticks_read(void)
{
    return mock_pit_ticks;
}

int frame_free(uint32_t physical_address)
{
    (void)physical_address;
    return 0;
}

uint32_t fat32_root_cluster(void)
{
    return 0u;
}

int fat32_read_file(uint32_t directory, const char *name, void *buffer,
                    uint32_t capacity, uint32_t *size)
{
    (void)directory;
    (void)name;
    (void)buffer;
    (void)capacity;
    (void)size;
    return 0;
}

uint32_t pci_device_count(void)
{
    return 1u;
}

int pci_get_device(uint32_t index, struct pci_device *device)
{
    if (index != 0u || device == 0) {
        return 0;
    }
    *device = test_device;
    return 1;
}

int pci_read_bar(const struct pci_device *device, uint8_t bar_index,
                 struct pci_bar *bar)
{
    if (device == 0 || bar == 0 || bar_index != 1u) {
        return 0;
    }
    *bar = test_bar;
    return 1;
}

int pci_enable_memory_space(const struct pci_device *device)
{
    return device != 0;
}

int pci_enable_bus_mastering(const struct pci_device *device)
{
    if (device == 0) {
        return 0;
    }
    bus_master_enabled = 1;
    return 1;
}

int paging_map_mmio(uint32_t address, uint32_t size)
{
    mapped_address = address;
    mapped_size = size;
    return map_succeeds;
}

static uint8_t mock_read8(void *context, uint16_t offset)
{
    (void)context;
    if (offset == 0x0030u) {
        uint16_t address = (uint16_t)mock_registers[0x0031u] |
            (uint16_t)((mock_registers[0x0032u] & 0x03u) << 8);
        return mock_efuse[address];
    }
    if (offset == 0x0033u) {
        return mock_efuse_ready ? 0x80u : 0u;
    }
    if (fail_clock_switch && offset == 0x0009u) {
        return 0u;
    }
    return mock_registers[offset];
}

static uint16_t mock_read16(void *context, uint16_t offset)
{
    (void)context;
    return (uint16_t)(mock_registers[offset] |
                      ((uint16_t)mock_registers[offset + 1u] << 8));
}

static uint32_t mock_read32(void *context, uint16_t offset)
{
    (void)context;
    if (offset == 0x0044u) {
        return mock_tcr;
    }
    return (uint32_t)mock_registers[offset] |
           ((uint32_t)mock_registers[offset + 1u] << 8) |
           ((uint32_t)mock_registers[offset + 2u] << 16) |
           ((uint32_t)mock_registers[offset + 3u] << 24);
}

static void mock_write8(void *context, uint16_t offset, uint8_t value)
{
    (void)context;
    mock_registers[offset] = value;
}

static void mock_write16(void *context, uint16_t offset, uint16_t value)
{
    (void)context;
    mock_registers[offset] = (uint8_t)value;
    mock_registers[offset + 1u] = (uint8_t)(value >> 8);
    if (offset == 0x0040u && value == 0x37fcu) {
        mock_tcr = 0x0au;
    } else if (offset == 0x0500u) {
        uint32_t descriptor_index = mock_poll_count % 16u;
        if (!suppress_dma_completion) {
            uint8_t *descriptor =
                &test_ring[descriptor_index * 64u];
            descriptor[3u] &= 0x7fu;
        }
        if (mock_poll_count == 0u) {
            mock_tcr = 0x03u;
        } else if (mock_poll_count == 1u) {
            mock_tcr = 0x0cu;
        } else {
            mock_tcr = 0xffu;
        }
        ++mock_poll_count;
    } else if (offset == 0x0002u || offset == 0x0008u) {
        mock_tcr |= 0x20u;
    }
}

static void mock_write32(void *context, uint16_t offset, uint32_t value)
{
    (void)context;
    mock_registers[offset] = (uint8_t)value;
    mock_registers[offset + 1u] = (uint8_t)(value >> 8);
    mock_registers[offset + 2u] = (uint8_t)(value >> 16);
    mock_registers[offset + 3u] = (uint8_t)(value >> 24);
    if (offset == 0x0044u) {
        mock_tcr = value;
    }
}

static void mock_delay(void *context, uint32_t usec)
{
    (void)context;
    if (usec != 0u) {
        ++delay_call_count;
    }
}

static int mock_enable_bus_mastering(void *context)
{
    (void)context;
    if ((test_ring[3u] & 0x80u) == 0u ||
        read_le32(&test_ring[28u]) == 0u ||
        read_le32(&test_ring[32u]) != 0x00200000u ||
        test_dma_buffer == 0) {
        return 0;
    }
    bus_master_enabled = 1;
    return 1;
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void reset_test_device(uint16_t vendor_id, uint16_t device_id)
{
    test_device = (struct pci_device) {
        .bus = 2u,
        .slot = 3u,
        .function = 1u,
        .vendor_id = vendor_id,
        .device_id = device_id,
    };
    test_bar = (struct pci_bar) {
        .address = 0xf0000000u,
        .is_io = 0u,
        .is_64bit = 0u,
    };
    mapped_address = 0u;
    mapped_size = 0u;
    map_succeeds = 1;
}

int main(void)
{
    struct rtl8192se_status status;
    uint8_t ring[16u * 64u];
    uint8_t scan_frame[80] = {0};
    struct rtl8192se_scan_result scan_result;
    scan_frame[0] = 0x80u;
    for (uint32_t index = 0u; index < 6u; ++index) {
        scan_frame[16u + index] = (uint8_t)(0x10u + index);
    }
    write_le16(&scan_frame[34u], 0x0011u);
    scan_frame[36u] = 0u;
    scan_frame[37u] = 4u;
    scan_frame[38u] = 'T';
    scan_frame[39u] = 'e';
    scan_frame[40u] = 's';
    scan_frame[41u] = 't';
    scan_frame[42u] = 3u;
    scan_frame[43u] = 1u;
    scan_frame[44u] = 6u;
    scan_frame[45u] = 48u;
    scan_frame[46u] = 18u;
    scan_frame[47u] = 1u;
    scan_frame[49u] = 0u;
    scan_frame[50u] = 0x0fu;
    scan_frame[51u] = 0xacu;
    scan_frame[52u] = 4u;
    scan_frame[53u] = 1u;
    scan_frame[55u] = 0u;
    scan_frame[56u] = 0x0fu;
    scan_frame[57u] = 0xacu;
    scan_frame[58u] = 4u;
    scan_frame[59u] = 1u;
    scan_frame[61u] = 0u;
    scan_frame[62u] = 0x0fu;
    scan_frame[63u] = 0xacu;
    scan_frame[64u] = 2u;
    if (!rtl8192se_parse_scan_frame(scan_frame, 65u, &scan_result) ||
        scan_result.ssid_length != 4u ||
        scan_result.ssid[0] != 'T' || scan_result.ssid[3] != 't' ||
        scan_result.channel != 6u ||
        scan_result.bssid[0] != 0x10u ||
        scan_result.bssid[5] != 0x15u ||
        scan_result.security != RTL8192SE_SECURITY_WPA2_PSK_CCMP ||
        rtl8192se_parse_scan_frame(scan_frame, 64u, &scan_result)) {
        fputs("RTL8191SE beacon/WPA2 scan-frame parsing failed\n", stderr);
        return 1;
    }
    scan_frame[0] = 0x50u;
    if (!rtl8192se_parse_scan_frame(scan_frame, 65u, &scan_result)) {
        fputs("RTL8191SE probe-response parsing failed\n", stderr);
        return 1;
    }
    scan_frame[0] = 0x80u;
    scan_frame[64u] = 1u;
    if (!rtl8192se_parse_scan_frame(scan_frame, 65u, &scan_result) ||
        scan_result.security != RTL8192SE_SECURITY_UNSUPPORTED) {
        fputs("RTL8191SE accepted unsupported WPA2 AKM as PSK\n", stderr);
        return 1;
    }
    write_le16(&scan_frame[34u], 1u);
    if (!rtl8192se_parse_scan_frame(scan_frame, 45u, &scan_result) ||
        scan_result.security != RTL8192SE_SECURITY_OPEN) {
        fputs("RTL8191SE open-network scan parsing failed\n", stderr);
        return 1;
    }
    scan_frame[0] = 0x40u;
    if (rtl8192se_parse_scan_frame(scan_frame, 65u, &scan_result)) {
        fputs("RTL8191SE scan parser accepted a non-beacon frame\n", stderr);
        return 1;
    }

    const uint8_t test_bssid[6] = {0x02u, 0x11u, 0x22u,
                                   0x33u, 0x44u, 0x55u};
    const uint8_t test_station[6] = {0x00u, 0x01u, 0x02u,
                                     0x03u, 0x04u, 0x05u};
    uint8_t management_frame[128] = {0};
    uint32_t management_length = 0u;
    uint8_t protocol_accepted = 0u;
    if (!rtl8192se_build_probe_request(
            test_station, 9u, management_frame, sizeof(management_frame),
            &management_length) ||
        management_length != 42u || management_frame[0] != 0x40u ||
        management_frame[4] != 0xffu || management_frame[9] != 0xffu ||
        management_frame[10] != test_station[0] ||
        management_frame[24] != 0u || management_frame[25] != 0u ||
        management_frame[26] != 1u || management_frame[27] != 8u ||
        management_frame[36] != 50u ||
        rtl8192se_build_probe_request(test_station, 0u, management_frame,
                                      41u, &management_length)) {
        fputs("RTL8191SE wildcard probe-request construction failed\n",
              stderr);
        return 1;
    }
    static const uint8_t target_ssid[] = {'A', 'P', 'T', 'O'};
    if (!rtl8192se_build_probe_request_for_ssid(
            test_station, target_ssid, sizeof(target_ssid), 9u,
            management_frame, sizeof(management_frame), &management_length) ||
        management_length != 46u || management_frame[24] != 0u ||
        management_frame[25] != sizeof(target_ssid) ||
        management_frame[26] != 'A' || management_frame[29] != 'O' ||
        management_frame[30] != 1u ||
        rtl8192se_build_probe_request_for_ssid(
            test_station, target_ssid, sizeof(target_ssid), 9u,
            management_frame, 45u, &management_length) ||
        rtl8192se_build_probe_request_for_ssid(
            test_station, 0, sizeof(target_ssid), 9u, management_frame,
            sizeof(management_frame), &management_length)) {
        fputs("RTL8191SE directed probe-request construction failed\n",
              stderr);
        return 1;
    }
    if (!rtl8192se_build_open_auth_request(
            test_bssid, test_station, 3u, management_frame,
            sizeof(management_frame), &management_length) ||
        management_length != 30u || management_frame[0] != 0xb0u ||
        management_frame[24] != 0u || management_frame[26] != 1u ||
        management_frame[28] != 0u) {
        fputs("RTL8191SE open-auth request construction failed\n", stderr);
        return 1;
    }
    management_frame[0] = 0xb0u;
    for (uint32_t index = 0u; index < 6u; ++index) {
        management_frame[4u + index] = test_station[index];
        management_frame[10u + index] = test_bssid[index];
        management_frame[16u + index] = test_bssid[index];
    }
    management_frame[24] = 0u;
    management_frame[26] = 2u;
    if (!rtl8192se_parse_open_auth_response(
            management_frame, 30u, test_bssid, test_station,
            &protocol_accepted) ||
        protocol_accepted == 0u) {
        fputs("RTL8191SE open-auth success response rejected\n", stderr);
        return 1;
    }
    management_frame[28] = 1u;
    if (!rtl8192se_parse_open_auth_response(
            management_frame, 30u, test_bssid, test_station,
            &protocol_accepted) ||
        protocol_accepted != 0u) {
        fputs("RTL8191SE open-auth failure response accepted\n", stderr);
        return 1;
    }
    static const uint8_t test_ssid[] = "Test";
    if (!rtl8192se_build_open_assoc_request(
            test_bssid, test_station, test_ssid, sizeof(test_ssid) - 1u,
            4u, management_frame, sizeof(management_frame),
            &management_length) ||
        management_length != 50u || management_frame[0] != 0u ||
        management_frame[24] != 1u || management_frame[28] != 0u ||
        management_frame[29] != 4u ||
        management_frame[30] != 'T' || management_frame[34] != 1u ||
        management_frame[44] != 50u ||
        rtl8192se_build_open_assoc_request(
            test_bssid, test_station, test_ssid, sizeof(test_ssid) - 1u,
            4u, management_frame, 40u, &management_length)) {
        fputs("RTL8191SE open-association request construction failed\n",
              stderr);
        return 1;
    }
    if (!rtl8192se_build_wpa2_assoc_request(
            test_bssid, test_station, test_ssid, sizeof(test_ssid) - 1u,
            4u, management_frame, sizeof(management_frame),
            &management_length) ||
        management_length != 72u || management_frame[24] != 0x11u ||
        management_frame[50] != 48u ||
        management_frame[51] != 20u || management_frame[52] != 1u ||
        management_frame[58] != 1u || management_frame[64] != 1u ||
        management_frame[69] != 2u || management_frame[70] != 0u ||
        rtl8192se_build_wpa2_assoc_request(
            test_bssid, test_station, test_ssid, sizeof(test_ssid) - 1u,
            4u, management_frame, 60u, &management_length)) {
        fprintf(stderr,
                "RTL8191SE WPA2 association request construction failed "
                "(length=%u rsn=%u,%u,%u,%u,%u,%u,%u,%u)\n",
                management_length, management_frame[50],
                management_frame[51], management_frame[52],
                management_frame[58], management_frame[64],
                management_frame[68], management_frame[70],
                management_frame[71]);
        return 1;
    }
    for (uint32_t index = 0u; index < sizeof(management_frame); ++index) {
        management_frame[index] = 0u;
    }
    management_frame[0] = 0x10u;
    for (uint32_t index = 0u; index < 6u; ++index) {
        management_frame[4u + index] = test_station[index];
        management_frame[10u + index] = test_bssid[index];
        management_frame[16u + index] = test_bssid[index];
    }
    management_frame[28] = 1u;
    management_frame[29] = 0xc0u;
    if (!rtl8192se_parse_open_assoc_response(
            management_frame, 30u, test_bssid, test_station,
            &protocol_accepted) ||
        protocol_accepted == 0u) {
        fputs("RTL8191SE open-association success response rejected\n",
              stderr);
        return 1;
    }
    management_frame[26] = 1u;
    if (!rtl8192se_parse_open_assoc_response(
            management_frame, 30u, test_bssid, test_station,
            &protocol_accepted) ||
        protocol_accepted != 0u) {
        fputs("RTL8191SE open-association failure response accepted\n",
              stderr);
        return 1;
    }
    static const uint8_t test_rsn_ie[22] = {
        48u, 20u, 1u, 0u, 0u, 0x0fu, 0xacu, 4u,
        1u, 0u, 0u, 0x0fu, 0xacu, 4u, 1u, 0u,
        0u, 0x0fu, 0xacu, 2u, 0u, 0u,
    };
    for (uint32_t index = 0u; index < sizeof(management_frame); ++index) {
        management_frame[index] = 0u;
    }
    management_frame[0] = 0x10u;
    for (uint32_t index = 0u; index < 6u; ++index) {
        management_frame[4u + index] = test_station[index];
        management_frame[10u + index] = test_bssid[index];
        management_frame[16u + index] = test_bssid[index];
    }
    management_frame[28] = 1u;
    management_frame[30] = 48u;
    management_frame[31] = 20u;
    for (uint32_t index = 2u; index < sizeof(test_rsn_ie); ++index) {
        management_frame[30u + index] = test_rsn_ie[index];
    }
    if (!rtl8192se_parse_wpa2_assoc_response(
            management_frame, 52u, test_bssid, test_station,
            &protocol_accepted) ||
        protocol_accepted == 0u) {
        fputs("RTL8191SE valid WPA2 association response rejected\n", stderr);
        return 1;
    }
    management_frame[43] = 2u;
    if (!rtl8192se_parse_wpa2_assoc_response(
            management_frame, 52u, test_bssid, test_station,
            &protocol_accepted) ||
        protocol_accepted != 0u) {
        fputs("RTL8191SE WPA2 response accepted a non-CCMP cipher\n", stderr);
        return 1;
    }

    struct rtl8192se_scan_result open_network = {
        .bssid = {0x02u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u},
        .ssid = {'T', 'e', 's', 't'},
        .ssid_length = 4u,
        .channel = 6u,
        .security = RTL8192SE_SECURITY_OPEN,
    };
    if (rtl8192se_network_is_associated() ||
        rtl8192se_open_association_start(&open_network) ||
        rtl8192se_scan_start() ||
        rtl8192se_scan_result_count() != 0u ||
        rtl8192se_scan_get_result(0u, &open_network)) {
        fputs("RTL8191SE advertised an association before firmware startup\n",
              stderr);
        return 1;
    }
    if (rtl8192se_scan_poll() != RTL8192SE_SCAN_IDLE) {
        fputs("RTL8191SE scan state was not initialized\n", stderr);
        return 1;
    }
    uint8_t ethernet_frame[34] = {
        0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u,
        0x02u, 0x07u, 0x08u, 0x09u, 0x0au, 0x0bu,
        0x08u, 0x00u, 0x45u, 0x00u, 0x00u, 0x14u,
        0x12u, 0x34u, 0x40u, 0x00u, 0x40u, 0x01u,
        0u, 0u, 0x0au, 0u, 0u, 2u, 0x0au, 0u, 0u, 1u,
    };
    uint8_t wireless_data[128];
    uint8_t ethernet_roundtrip[64];
    uint32_t wireless_length = 0u;
    uint32_t ethernet_length = 0u;
    if (!rtl8192se_encapsulate_ethernet(
            ethernet_frame, sizeof(ethernet_frame), test_bssid, test_station,
            0x123u, wireless_data, sizeof(wireless_data), &wireless_length) ||
        wireless_length != 32u + sizeof(ethernet_frame) - 14u ||
        wireless_data[0] != 0x08u || wireless_data[1] != 0x01u ||
        wireless_data[22] != 0x30u || wireless_data[23] != 0x12u) {
        fputs("RTL8191SE Ethernet encapsulation failed\n", stderr);
        return 1;
    }
    wireless_data[1] = 0x02u;
    for (uint32_t index = 0u; index < 6u; ++index) {
        wireless_data[4u + index] = test_station[index];
        wireless_data[10u + index] = test_bssid[index];
        wireless_data[16u + index] = ethernet_frame[6u + index];
    }
    if (
        !rtl8192se_decapsulate_ethernet(
            wireless_data, wireless_length, test_bssid, test_station,
            ethernet_roundtrip, sizeof(ethernet_roundtrip),
            &ethernet_length) ||
        ethernet_length != sizeof(ethernet_frame)) {
        fputs("RTL8191SE Ethernet/802.11 bridge conversion failed\n", stderr);
        return 1;
    }
    for (uint32_t index = 0u; index < sizeof(ethernet_frame); ++index) {
        if (ethernet_frame[index] != ethernet_roundtrip[index]) {
            fputs("RTL8191SE Ethernet bridge changed frame contents\n", stderr);
            return 1;
        }
    }
    wireless_data[1] = 0x03u;
    if (rtl8192se_decapsulate_ethernet(
            wireless_data, wireless_length, test_bssid, test_station,
            ethernet_roundtrip, sizeof(ethernet_roundtrip),
            &ethernet_length)) {
        fputs("RTL8191SE bridge accepted unsupported four-address frame\n",
              stderr);
        return 1;
    }

    if (!rtl8192se_build_command_ring(ring, 0x00100000u, 16u) ||
        read_le32(&ring[36u]) != 0x00100040u ||
        read_le32(&ring[15u * 64u + 36u]) != 0x00100000u ||
        rtl8192se_build_command_ring(ring, 0x00100001u, 16u) ||
        rtl8192se_build_command_ring(ring, 0x00100000u, 17u) ||
        rtl8192se_build_command_ring(ring, 0x00ffffc0u, 16u)) {
        fputs("RTL8191SE command DMA ring layout is invalid\n", stderr);
        return 1;
    }

    uint8_t rx_ring[16u * 32u] __attribute__((aligned(256)));
    uint32_t rx_buffer_addresses[16u];
    for (uint32_t index = 0u; index < 16u; ++index) {
        rx_buffer_addresses[index] = 0x00300000u + index * 4096u;
    }
    if (!rtl8192se_build_rx_ring(rx_ring, 0x00110000u,
                                 rx_buffer_addresses, 16u, 4096u) ||
        read_le32(&rx_ring[0u]) != 0x80001000u ||
        read_le32(&rx_ring[24u]) != rx_buffer_addresses[0u] ||
        read_le32(&rx_ring[15u * 32u]) != 0xc0001000u ||
        read_le32(&rx_ring[15u * 32u + 24u]) !=
            rx_buffer_addresses[15u]) {
        fputs("RTL8191SE RX descriptor ring layout is invalid\n", stderr);
        return 1;
    }
    if (!rtl8192se_rearm_rx_descriptor(
            &rx_ring[3u * 32u], rx_buffer_addresses[3u], 4096u, 1) ||
        read_le32(&rx_ring[3u * 32u]) != 0xc0001000u ||
        read_le32(&rx_ring[3u * 32u + 24u]) != rx_buffer_addresses[3u] ||
        rtl8192se_rearm_rx_descriptor(&rx_ring[3u * 32u],
                                      0x01000000u, 4096u, 0)) {
        fputs("RTL8191SE RX descriptor rearm failed\n", stderr);
        return 1;
    }
    rx_buffer_addresses[3u] = 0x01000000u;
    if (rtl8192se_build_rx_ring(rx_ring, 0x00110000u,
                                rx_buffer_addresses, 16u, 4096u)) {
        fputs("RTL8191SE RX ring accepted an out-of-range DMA buffer\n",
              stderr);
        return 1;
    }

    uint8_t tx_descriptor[64u] __attribute__((aligned(64)));
    if (!rtl8192se_build_tx_descriptor(tx_descriptor, 0x00300000u, 128u,
                                       0x00120040u, 0x12u, 1u) ||
        read_le32(tx_descriptor) != 0x8c200080u ||
        read_le32(&tx_descriptor[4u]) != 0x00011200u ||
        read_le32(&tx_descriptor[16u]) != 0x80000000u ||
        read_le32(&tx_descriptor[28u]) != 128u ||
        read_le32(&tx_descriptor[32u]) != 0x00300000u ||
        read_le32(&tx_descriptor[36u]) != 0x00120040u ||
        !rtl8192se_build_tx_descriptor(tx_descriptor, 0x00300000u, 128u,
                                       0x00120040u, 0x12u, 0u) ||
        read_le32(&tx_descriptor[4u]) != 0x00001200u ||
        rtl8192se_build_tx_descriptor(tx_descriptor, 0x01000000u, 128u,
                                      0x00120040u, 0x12u, 1u) ||
        rtl8192se_build_tx_descriptor(tx_descriptor, 0x00300000u, 4097u,
                                      0x00120040u, 0x12u, 1u)) {
        fputs("RTL8191SE TX descriptor construction failed\n", stderr);
        return 1;
    }

    uint8_t rx_descriptor[32u] = {0};
    uint8_t rx_buffer[64u] = {0};
    uint8_t received_frame[32u] = {0};
    uint32_t received_length = 0u;
    for (uint32_t index = 0u; index < 24u; ++index) {
        rx_buffer[index + 10u] = (uint8_t)(0xa0u + index);
    }
    write_le32(rx_descriptor, (1u << 16) | (2u << 24) | 24u);
    if (!rtl8192se_rx_copy_frame(rx_descriptor, rx_buffer,
                                 sizeof(rx_buffer), received_frame,
                                 sizeof(received_frame), &received_length) ||
        received_length != 20u || received_frame[0] != 0xa0u ||
        received_frame[19] != 0xb3u ||
        rtl8192se_rx_copy_frame(rx_descriptor, rx_buffer, 32u,
                                received_frame, sizeof(received_frame),
                                &received_length)) {
        fputs("RTL8191SE RX frame extraction failed\n", stderr);
        return 1;
    }
    write_le32(rx_descriptor, (1u << 16) | (2u << 24) | 4u);
    if (rtl8192se_rx_copy_frame(rx_descriptor, rx_buffer,
                                sizeof(rx_buffer), received_frame,
                                sizeof(received_frame), &received_length)) {
        fputs("RTL8191SE RX extraction accepted an FCS-only payload\n",
              stderr);
        return 1;
    }
    write_le32(rx_descriptor, 0x80000018u);
    if (rtl8192se_rx_copy_frame(rx_descriptor, rx_buffer,
                                sizeof(rx_buffer), received_frame,
                                sizeof(received_frame), &received_length)) {
        fputs("RTL8191SE RX frame extraction ignored descriptor ownership\n",
              stderr);
        return 1;
    }
    write_le32(rx_descriptor, (1u << 14) | 24u);
    if (rtl8192se_rx_copy_frame(rx_descriptor, rx_buffer,
                                sizeof(rx_buffer), received_frame,
                                sizeof(received_frame), &received_length)) {
        fputs("RTL8191SE RX frame extraction accepted a CRC error\n",
              stderr);
        return 1;
    }

    struct rtl8192se_register_io io = {
        .read8 = mock_read8,
        .read16 = mock_read16,
        .read32 = mock_read32,
        .write8 = mock_write8,
        .write16 = mock_write16,
        .write32 = mock_write32,
        .delay_us = mock_delay,
        .enable_bus_mastering = mock_enable_bus_mastering,
    };
    struct rtl8192se_adapter_config phy_config = {
        .mac_address = {0x02u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u},
        .rf_configuration = 0x12u,
        .channel_plan = 0x34u,
        .cck_tx_power = {0x12u, 0x24u, 0x36u},
        .cck_tx_power_valid_mask = 0x07u,
    };
    if (!rtl8192se_initialize_phy(&io, &phy_config) ||
        mock_registers[0x50u] != phy_config.mac_address[0] ||
        mock_registers[0x55u] != phy_config.mac_address[5] ||
        (read_le32(&mock_registers[0xe08u]) & 0x00007f00u) != 0x00001200u ||
        read_le32(&mock_registers[0x840u]) != 0x0180f401u ||
        read_le32(&mock_registers[0x844u]) != 0x0180f401u ||
        !rtl8192se_set_channel(&io, 13u) ||
        (read_le32(&mock_registers[0xe08u]) & 0x00007f00u) != 0x00003600u ||
        read_le32(&mock_registers[0x840u]) != 0x0180f40du ||
        read_le32(&mock_registers[0x844u]) != 0x0180f40du ||
        !rtl8192se_set_channel(&io, 8u) ||
        (read_le32(&mock_registers[0xe08u]) & 0x00007f00u) != 0x00002400u ||
        !rtl8192se_set_channel(&io, 9u) ||
        (read_le32(&mock_registers[0xe08u]) & 0x00007f00u) != 0x00003600u ||
        rtl8192se_set_channel(&io, 14u)) {
        fputs("RTL8191SE PHY/RF startup or channel tuning failed\n", stderr);
        return 1;
    }
    if (!rtl8192se_program_rx_rings(&io, 0x00110000u, 0x00120000u) ||
        read_le32(&mock_registers[0x053cu]) != 0x00110000u ||
        read_le32(&mock_registers[0x0538u]) != 0x00120000u ||
        rtl8192se_program_rx_rings(&io, 0x00110001u, 0x00120000u)) {
        fputs("RTL8191SE RX ring base programming failed\n", stderr);
        return 1;
    }
    if (!rtl8192se_program_tx_rings(&io, 0x00130000u, 0x00140000u) ||
        read_le32(&mock_registers[0x052cu]) != 0x00130000u ||
        read_le32(&mock_registers[0x0518u]) != 0x00140000u ||
        rtl8192se_program_tx_rings(&io, 0x00130001u, 0x00140000u)) {
        fputs("RTL8191SE TX ring base programming failed\n", stderr);
        return 1;
    }
    if (!rtl8192se_enable_mac_io(&io) ||
        read_le32(&mock_registers[0x0048u]) != 0x031f000fu ||
        mock_registers[0x00b6u] != 4u ||
        mock_registers[0x00bdu] != 0x40u ||
        ((uint16_t)mock_registers[0x0040u] |
         ((uint16_t)mock_registers[0x0041u] << 8)) != 0x37fcu) {
        fputs("RTL8191SE MAC/RX/TX engines failed to enable\n", stderr);
        return 1;
    }
    if (!rtl8192se_build_command_ring(test_ring, 0x00100000u, 16u) ||
        !rtl8192se_mac_power_sequence(
            &io, 0x00100000u, 0x00110000u, 0x00120000u,
            0x00130000u, 0x00140000u) ||
        delay_call_count == 0u || mock_registers[0x0040u] != 0xfcu ||
        mock_registers[0x0041u] != 0x37u ||
        mock_registers[0x051cu] != 0x00u ||
        mock_registers[0x051du] != 0x00u ||
        mock_registers[0x051eu] != 0x10u ||
        mock_registers[0x051fu] != 0x00u ||
        read_le32(&mock_registers[0x053cu]) != 0x00110000u ||
        read_le32(&mock_registers[0x0538u]) != 0x00120000u ||
        read_le32(&mock_registers[0x052cu]) != 0x00130000u ||
        read_le32(&mock_registers[0x0518u]) != 0x00140000u ||
        mock_registers[0x00a0u] != 0xffu ||
        mock_registers[0x00a8u] != 0xffu ||
        mock_registers[0x00abu] != 0x80u ||
        mock_registers[0x0503u] != 0x22u ||
        mock_registers[0x00dcu] != 0xffu ||
        mock_registers[0x011au] != 0u ||
        mock_registers[0x0160u] != 0x1bu ||
        mock_registers[0x017fu] != 0x1bu ||
        mock_registers[0x0236u] != 0xffu ||
        mock_registers[0x003au] != 0x91u) {
        fputs("RTL8191SE MAC power preparation failed\n", stderr);
        return 1;
    }

    for (uint32_t index = 0u; index < sizeof(mock_registers); ++index) {
        mock_registers[index] = 0u;
    }
    fail_clock_switch = 1;
    if (rtl8192se_mac_power_sequence(
            &io, 0x00100000u, 0x00110000u, 0x00120000u,
            0x00130000u, 0x00140000u)) {
        fputs("RTL8191SE ignored a failed MAC clock switch\n", stderr);
        return 1;
    }
    fail_clock_switch = 0;

    uint8_t fake_firmware[135] = {0};
    fake_firmware[38u] = 0x22u;
    for (uint32_t index = 80u; index < sizeof(fake_firmware); ++index) {
        fake_firmware[index] = (uint8_t)(index + 1u);
    }
    struct rtl8192se_firmware firmware = {
        .bytes = fake_firmware,
        .size = sizeof(fake_firmware),
        .version = 1u,
        .imem_offset = 80u,
        .imem_size = 4u,
        .emem_offset = 84u,
        .emem_size = 3u,
        .dmem_offset = 32u,
        .dmem_size = 48u,
    };
    test_dma_buffer = fake_firmware;
    for (uint32_t index = 0u; index < sizeof(mock_registers); ++index) {
        mock_registers[index] = 0u;
    }
    mock_tcr = 0x0au;
    mock_poll_count = 0u;
    bus_master_enabled = 0;
    enum rtl8192se_firmware_upload_failure upload_failure =
        RTL8192SE_FW_UPLOAD_INVALID_INPUT;
    uint32_t observed_tcr = 0u;
    if (!rtl8192se_build_command_ring(test_ring, 0x00100000u, 16u) ||
        !rtl8192se_upload_firmware(&io, &firmware, test_ring,
                                   0x00100000u, 16u, fake_firmware,
                                   0x00200000u, 4096u, 0x22u,
                                   &upload_failure, &observed_tcr) ||
        !bus_master_enabled || mock_poll_count != 3u) {
        fputs("RTL8191SE firmware download sequence failed\n", stderr);
        return 1;
    }
    if (upload_failure != RTL8192SE_FW_UPLOAD_OK ||
        observed_tcr != 0xffu) {
        fputs("RTL8191SE firmware upload completion status was not recorded\n",
              stderr);
        return 1;
    }

    mock_tcr = 0x0au;
    mock_poll_count = 0u;
    bus_master_enabled = 0;
    suppress_dma_completion = 1;
    if (rtl8192se_upload_firmware(&io, &firmware, test_ring,
                                  0x00100000u, 16u, fake_firmware,
                                  0x00200000u, 4096u, 0x22u,
                                  &upload_failure, &observed_tcr) ||
        upload_failure != RTL8192SE_FW_UPLOAD_IMEM_TRANSFER ||
        observed_tcr != 0x03u) {
        fputs("RTL8191SE firmware upload ignored a stuck DMA descriptor\n",
              stderr);
        return 1;
    }
    suppress_dma_completion = 0;

    for (uint32_t index = 0u; index < sizeof(mock_efuse); ++index) {
        mock_efuse[index] = 0xffu;
    }
    mock_efuse[0u] = 0x0eu;
    mock_efuse[1u] = 0x29u;
    mock_efuse[2u] = 0x81u;
    mock_efuse[3u] = 0x21u;
    mock_efuse[4u] = 0x00u;
    mock_efuse[5u] = 0x11u;
    mock_efuse[6u] = 0x22u;
    mock_efuse[7u] = 0x33u;
    mock_efuse[8u] = 0x44u;
    mock_efuse[9u] = 0x55u;
    mock_efuse[10u] = 0xf5u;
    mock_efuse[11u] = 0x00u;
    mock_efuse[12u] = 0x34u;
    mock_efuse[13u] = 0x01u;
    mock_efuse[14u] = 0x00u;
    mock_efuse[15u] = 0xacu;
    mock_efuse[16u] = 0x12u;
    mock_efuse[17u] = 0x24u;
    mock_efuse[18u] = 0x36u;
    mock_efuse[19u] = 0x48u;
    mock_efuse[20u] = 0xffu;
    struct rtl8192se_adapter_config adapter_config;
    if (!rtl8192se_parse_efuse(mock_efuse, sizeof(mock_efuse),
                               &adapter_config) ||
        adapter_config.mac_address[0] != 0x00u ||
        adapter_config.mac_address[5] != 0x55u ||
        adapter_config.channel_plan != 0x34u ||
        adapter_config.rf_configuration != 0x12u ||
        adapter_config.cck_tx_power[0] != 0x12u ||
        adapter_config.cck_tx_power[1] != 0x24u ||
        adapter_config.cck_tx_power[2] != 0x36u ||
        adapter_config.cck_tx_power_valid_mask != 0x07u) {
        fputs("RTL8191SE eFuse logical map parsing failed\n", stderr);
        return 1;
    }
    mock_efuse[1u] = 0u;
    if (rtl8192se_parse_efuse(mock_efuse, sizeof(mock_efuse),
                              &adapter_config)) {
        fputs("RTL8191SE accepted an invalid eFuse signature\n", stderr);
        return 1;
    }
    mock_efuse[1u] = 0x29u;
    if (!rtl8192se_read_adapter_config(&io, &adapter_config) ||
        adapter_config.mac_address[0] != 0x00u ||
        adapter_config.mac_address[5] != 0x55u ||
        adapter_config.channel_plan != 0x34u ||
        adapter_config.rf_configuration != 0x12u) {
        fputs("RTL8191SE eFuse adapter configuration parsing failed\n",
              stderr);
        return 1;
    }
    mock_efuse[13u] = 0x02u;
    if (!rtl8192se_parse_efuse(mock_efuse, sizeof(mock_efuse),
                               &adapter_config) ||
        adapter_config.rf_configuration != 0x12u) {
        fputs("RTL8191SE mapped a one-TX/two-RX RF board incorrectly\n",
              stderr);
        return 1;
    }
    mock_efuse[13u] = 0x03u;
    if (!rtl8192se_parse_efuse(mock_efuse, sizeof(mock_efuse),
                               &adapter_config) ||
        adapter_config.rf_configuration != 0x11u) {
        fputs("RTL8191SE mapped a one-TX/one-RX RF board incorrectly\n",
              stderr);
        return 1;
    }
    mock_efuse[13u] = 0x01u;
    mock_efuse[13u] = 0xffu;
    if (rtl8192se_read_adapter_config(&io, &adapter_config)) {
        fputs("RTL8191SE accepted an invalid eFuse board type\n", stderr);
        return 1;
    }
    mock_efuse[13u] = 0x01u;
    mock_efuse_ready = 0;
    if (rtl8192se_read_adapter_config(&io, &adapter_config)) {
        fputs("RTL8191SE ignored an eFuse read timeout\n", stderr);
        return 1;
    }
    mock_efuse_ready = 1;

    reset_test_device(0x10ecu, 0x8171u);
    if (!rtl8192se_probe(&status) || status.probed == 0u ||
        status.device_id != 0x8171u || status.mmio_base != 0xf0000000u ||
        status.bus != 2u || status.slot != 3u || status.function != 1u ||
        mapped_address != 0xf0000000u || mapped_size != 0x4000u) {
        fputs("RTL8191SE PCI/MMIO probe rejected supported device\n", stderr);
        return 1;
    }

    reset_test_device(0x10ecu, 0x8175u);
    if (rtl8192se_probe(&status) || status.probed != 0u ||
        mapped_address != 0u) {
        fputs("RTL8192SE probe accepted unsupported device ID\n", stderr);
        return 1;
    }

    reset_test_device(0x10ecu, 0x8171u);
    test_bar.is_io = 1u;
    if (rtl8192se_probe(&status) || status.probed != 0u ||
        mapped_address != 0u) {
        fputs("RTL8192SE probe accepted an I/O BAR for MMIO\n", stderr);
        return 1;
    }

    reset_test_device(0x10ecu, 0x8171u);
    test_bar.address = 0x100000000ull;
    if (rtl8192se_probe(&status) || status.probed != 0u ||
        mapped_address != 0u) {
        fputs("RTL8192SE probe accepted an unmappable 64-bit BAR\n", stderr);
        return 1;
    }

    reset_test_device(0x10ecu, 0x8171u);
    map_succeeds = 0;
    if (rtl8192se_probe(&status) || status.probed != 0u) {
        fputs("RTL8192SE probe ignored MMIO mapping failure\n", stderr);
        return 1;
    }
    return 0;
}
