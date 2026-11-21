#include <stdint.h>

#include "frame_allocator.h"
#include "paging.h"
#include "pit.h"
#include "pci.h"
#include "rtl8192se.h"
#include "rtl8192se_firmware.h"
#include "rtl8192se_hw.h"
#include "secure_random.h"
#include "wpa2_ccmp_frame.h"
#include "wpa2_handshake.h"

#define RTL8192SE_VENDOR_ID 0x10ecu
#define RTL8192SE_REGISTER_BAR 1u
#define RTL8192SE_REGISTER_WINDOW_SIZE 0x4000u
#define RTL8192SE_DMA_LIMIT 0x01000000ull
#define RTL8192SE_PAGE_SIZE 4096u
#define RTL8192SE_COMMAND_RING_COUNT 16u
#define RTL8192SE_DESCRIPTOR_SIZE 64u
#define RTL8192SE_DESCRIPTOR_NEXT_OFFSET 36u
#define RTL8192SE_RX_RING_COUNT 16u
#define RTL8192SE_RX_DESCRIPTOR_SIZE 32u
#define RTL8192SE_RX_BUFFER_SIZE RTL8192SE_PAGE_SIZE
#define RTL8192SE_TX_RING_COUNT 16u
#define RTL8192SE_TX_BUFFER_SIZE RTL8192SE_PAGE_SIZE
#define RTL8192SE_TX_POLL_REGISTER 0x0500u
#define RTL8192SE_TX_DATA_POLL (1u << 1)
#define RTL8192SE_TX_MANAGEMENT_POLL (1u << 6)
#define RTL8192SE_TX_DATA_QUEUE_SELECTION 0u
#define RTL8192SE_TX_MANAGEMENT_QUEUE_SELECTION 0x12u
#define RTL8192SE_ETHERNET_FRAME_MAX 1536u
#define RTL8192SE_80211_FRAME_MAX 1600u
#define RTL8192SE_ASSOCIATION_TIMEOUT_TICKS 300u
#define RTL8192SE_AUTH_RETRY_INTERVAL_TICKS 50u
#define RTL8192SE_AUTH_RETRY_LIMIT 3u
#define RTL8192SE_AUTH_TX_DESCRIPTOR_COUNT (RTL8192SE_AUTH_RETRY_LIMIT + 1u)
#define RTL8192SE_SCAN_FIRST_CHANNEL 1u
#define RTL8192SE_SCAN_LAST_CHANNEL 13u
#define RTL8192SE_SCAN_DWELL_TICKS 50u
#define RTL8192SE_SCAN_RESULT_MAX 32u
#define RTL8192SE_EAPOL_SNAP_LENGTH 8u

static struct rtl8192se_status adapter_status;
static uint8_t associated_bssid[6];
static uint16_t data_sequence_number;
static uint8_t wireless_frame_buffer[RTL8192SE_80211_FRAME_MAX];
static uint8_t ethernet_frame_buffer[RTL8192SE_ETHERNET_FRAME_MAX];
static uint8_t wifi_associated;
static uint8_t association_security_wpa2;
static uint8_t association_ssid[32];
static uint8_t association_ssid_length;
static struct wpa2_handshake wpa2_session;
static enum rtl8192se_association_failure association_failure;
static uint32_t association_rx_frames;
static uint32_t association_auth_responses;
static uint32_t association_auth_tx_submissions;
static uint32_t association_auth_tx_dma_completions;
static uint32_t association_rx_invalid_descriptors;
static uint8_t *association_auth_tx_descriptors[
    RTL8192SE_AUTH_TX_DESCRIPTOR_COUNT];
static uint32_t association_auth_tx_descriptor_count;
static uint8_t *last_management_tx_descriptor;
static uint64_t pairwise_tx_packet_number;
static uint64_t group_tx_packet_number;
static uint64_t pairwise_rx_packet_number;
static uint64_t group_rx_packet_number;
static enum rtl8192se_association_state association_state;
static enum rtl8192se_scan_state scan_state;
static struct rtl8192se_scan_result scan_results[RTL8192SE_SCAN_RESULT_MAX];
static uint32_t scan_result_count;
static uint32_t scan_received_frame_count;
static uint32_t scan_parsed_frame_count;
static uint32_t scan_management_frame_count;
static uint32_t scan_beacon_frame_count;
static uint32_t scan_probe_response_frame_count;
static uint32_t scan_other_frame_count;
static uint32_t scan_short_frame_count;
static uint32_t scan_deadline;
static uint8_t scan_channel;
static uint8_t scan_target_ssid[32];
static uint8_t scan_target_ssid_length;
static uint32_t association_deadline;
static uint32_t association_retry_deadline;
static uint16_t management_sequence_number;
static uint16_t association_request_sequence;
static uint8_t association_retry_count;
static uint32_t command_ring_frame = FRAME_INVALID;
static uint32_t command_buffer_frame = FRAME_INVALID;
static uint32_t rx_mpdu_ring_frame = FRAME_INVALID;
static uint32_t rx_command_ring_frame = FRAME_INVALID;
static uint32_t tx_data_ring_frame = FRAME_INVALID;
static uint32_t tx_management_ring_frame = FRAME_INVALID;
static uint32_t rx_mpdu_buffer_frames[RTL8192SE_RX_RING_COUNT];
static uint32_t rx_command_buffer_frames[RTL8192SE_RX_RING_COUNT];
static uint32_t tx_data_buffer_frames[RTL8192SE_TX_RING_COUNT];
static uint32_t tx_management_buffer_frames[RTL8192SE_TX_RING_COUNT];
static uint32_t tx_data_index;
static uint32_t tx_management_index;
static uint32_t rx_mpdu_index;
static uint8_t command_dma_ready;
static uint8_t rx_dma_ready;
static uint8_t tx_dma_ready;
static uint8_t command_bus_master_enabled;

static int rtl8192se_set_current_channel(uint8_t channel);

static void write_le32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint32_t read_volatile_le32(const volatile uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void rtl8192se_reset_association_diagnostics(void)
{
    association_rx_frames = 0u;
    association_auth_responses = 0u;
    association_auth_tx_submissions = 0u;
    association_auth_tx_dma_completions = 0u;
    association_rx_invalid_descriptors = 0u;
    association_auth_tx_descriptor_count = 0u;
    for (uint32_t index = 0u; index < RTL8192SE_AUTH_TX_DESCRIPTOR_COUNT;
         ++index) {
        association_auth_tx_descriptors[index] = 0;
    }
}

static void rtl8192se_record_auth_tx_submission(void)
{
    if (last_management_tx_descriptor == 0 ||
        association_auth_tx_descriptor_count >=
            RTL8192SE_AUTH_TX_DESCRIPTOR_COUNT) {
        return;
    }
    association_auth_tx_descriptors[association_auth_tx_descriptor_count++] =
        last_management_tx_descriptor;
    ++association_auth_tx_submissions;
}

static int rtl8192se_update_auth_tx_completions(void)
{
    int pending = 0;
    for (uint32_t index = 0u;
         index < association_auth_tx_descriptor_count; ++index) {
        uint8_t *descriptor = association_auth_tx_descriptors[index];
        if (descriptor == 0) {
            continue;
        }
        if ((read_volatile_le32(descriptor) & (1u << 31)) != 0u) {
            pending = 1;
            continue;
        }
        association_auth_tx_descriptors[index] = 0;
        ++association_auth_tx_dma_completions;
    }
    return pending;
}

int rtl8192se_build_command_ring(uint8_t *ring, uint32_t physical_address,
                                uint32_t descriptor_count)
{
    if (ring == 0 || descriptor_count < 2u ||
        descriptor_count > RTL8192SE_COMMAND_RING_COUNT ||
        (physical_address & 0xffu) != 0u ||
        physical_address >= RTL8192SE_DMA_LIMIT ||
        descriptor_count * RTL8192SE_DESCRIPTOR_SIZE >
            RTL8192SE_PAGE_SIZE ||
        physical_address >
            RTL8192SE_DMA_LIMIT -
                descriptor_count * RTL8192SE_DESCRIPTOR_SIZE) {
        return 0;
    }

    for (uint32_t index = 0u; index < descriptor_count; ++index) {
        uint8_t *descriptor = ring + index * RTL8192SE_DESCRIPTOR_SIZE;
        for (uint32_t byte = 0u; byte < RTL8192SE_DESCRIPTOR_SIZE; ++byte) {
            descriptor[byte] = 0u;
        }
        uint32_t next = physical_address +
            ((index + 1u) % descriptor_count) * RTL8192SE_DESCRIPTOR_SIZE;
        write_le32(&descriptor[RTL8192SE_DESCRIPTOR_NEXT_OFFSET], next);
    }
    return 1;
}

static void release_command_dma(void)
{
    if (command_buffer_frame != FRAME_INVALID) {
        frame_free(command_buffer_frame);
        command_buffer_frame = FRAME_INVALID;
    }
    if (command_ring_frame != FRAME_INVALID) {
        frame_free(command_ring_frame);
        command_ring_frame = FRAME_INVALID;
    }
    command_dma_ready = 0u;
}

int rtl8192se_prepare_command_dma(struct rtl8192se_status *status)
{
    if (status == 0 || status->probed == 0u) {
        return 0;
    }
    if (command_dma_ready != 0u) {
        status->command_ring = command_ring_frame;
        status->dma_prepared = 1u;
        return 1;
    }

    command_ring_frame = frame_allocate_below(RTL8192SE_DMA_LIMIT);
    if (command_ring_frame == FRAME_INVALID) {
        return 0;
    }

    uint8_t *ring = (uint8_t *)(uintptr_t)command_ring_frame;
    if (!rtl8192se_build_command_ring(ring, command_ring_frame,
                                      RTL8192SE_COMMAND_RING_COUNT)) {
        release_command_dma();
        return 0;
    }
    command_buffer_frame = frame_allocate_below(RTL8192SE_DMA_LIMIT);
    if (command_buffer_frame == FRAME_INVALID) {
        release_command_dma();
        return 0;
    }
    uint8_t *buffer = (uint8_t *)(uintptr_t)command_buffer_frame;
    for (uint32_t byte = 0u; byte < RTL8192SE_PAGE_SIZE; ++byte) {
        buffer[byte] = 0u;
    }

    __asm__ volatile ("" : : : "memory");
    command_dma_ready = 1u;
    status->command_ring = command_ring_frame;
    status->dma_prepared = 1u;
    return 1;
}

static void release_rx_dma(void)
{
    for (uint32_t index = 0u; index < RTL8192SE_RX_RING_COUNT; ++index) {
        if (rx_mpdu_buffer_frames[index] != FRAME_INVALID) {
            frame_free(rx_mpdu_buffer_frames[index]);
            rx_mpdu_buffer_frames[index] = FRAME_INVALID;
        }
        if (rx_command_buffer_frames[index] != FRAME_INVALID) {
            frame_free(rx_command_buffer_frames[index]);
            rx_command_buffer_frames[index] = FRAME_INVALID;
        }
    }
    if (rx_mpdu_ring_frame != FRAME_INVALID) {
        frame_free(rx_mpdu_ring_frame);
        rx_mpdu_ring_frame = FRAME_INVALID;
    }
    if (rx_command_ring_frame != FRAME_INVALID) {
        frame_free(rx_command_ring_frame);
        rx_command_ring_frame = FRAME_INVALID;
    }
    rx_dma_ready = 0u;
}

static int rtl8192se_prepare_rx_dma(struct rtl8192se_status *status)
{
    if (status == 0 || status->probed == 0u) {
        return 0;
    }
    if (rx_dma_ready != 0u) {
        status->rx_mpdu_ring = rx_mpdu_ring_frame;
        status->rx_command_ring = rx_command_ring_frame;
        status->rx_dma_prepared = 1u;
        return 1;
    }

    for (uint32_t index = 0u; index < RTL8192SE_RX_RING_COUNT; ++index) {
        rx_mpdu_buffer_frames[index] = FRAME_INVALID;
        rx_command_buffer_frames[index] = FRAME_INVALID;
    }
    rx_mpdu_ring_frame = frame_allocate_below(RTL8192SE_DMA_LIMIT);
    rx_command_ring_frame = frame_allocate_below(RTL8192SE_DMA_LIMIT);
    if (rx_mpdu_ring_frame == FRAME_INVALID ||
        rx_command_ring_frame == FRAME_INVALID) {
        release_rx_dma();
        return 0;
    }

    uint32_t mpdu_buffers[RTL8192SE_RX_RING_COUNT];
    uint32_t command_buffers[RTL8192SE_RX_RING_COUNT];
    for (uint32_t index = 0u; index < RTL8192SE_RX_RING_COUNT; ++index) {
        rx_mpdu_buffer_frames[index] =
            frame_allocate_below(RTL8192SE_DMA_LIMIT);
        rx_command_buffer_frames[index] =
            frame_allocate_below(RTL8192SE_DMA_LIMIT);
        if (rx_mpdu_buffer_frames[index] == FRAME_INVALID ||
            rx_command_buffer_frames[index] == FRAME_INVALID) {
            release_rx_dma();
            return 0;
        }
        mpdu_buffers[index] = rx_mpdu_buffer_frames[index];
        command_buffers[index] = rx_command_buffer_frames[index];
        uint8_t *mpdu_buffer =
            (uint8_t *)(uintptr_t)rx_mpdu_buffer_frames[index];
        uint8_t *command_buffer =
            (uint8_t *)(uintptr_t)rx_command_buffer_frames[index];
        for (uint32_t byte = 0u; byte < RTL8192SE_RX_BUFFER_SIZE; ++byte) {
            mpdu_buffer[byte] = 0u;
            command_buffer[byte] = 0u;
        }
    }

    uint8_t *mpdu_ring = (uint8_t *)(uintptr_t)rx_mpdu_ring_frame;
    uint8_t *command_ring = (uint8_t *)(uintptr_t)rx_command_ring_frame;
    if (!rtl8192se_build_rx_ring(mpdu_ring, rx_mpdu_ring_frame,
                                 mpdu_buffers, RTL8192SE_RX_RING_COUNT,
                                 RTL8192SE_RX_BUFFER_SIZE) ||
        !rtl8192se_build_rx_ring(command_ring, rx_command_ring_frame,
                                 command_buffers, RTL8192SE_RX_RING_COUNT,
                                 RTL8192SE_RX_BUFFER_SIZE)) {
        release_rx_dma();
        return 0;
    }

    __asm__ volatile ("" : : : "memory");
    rx_dma_ready = 1u;
    rx_mpdu_index = 0u;
    status->rx_mpdu_ring = rx_mpdu_ring_frame;
    status->rx_command_ring = rx_command_ring_frame;
    status->rx_dma_prepared = 1u;
    return 1;
}

static void release_tx_dma(void)
{
    for (uint32_t index = 0u; index < RTL8192SE_TX_RING_COUNT; ++index) {
        if (tx_data_buffer_frames[index] != FRAME_INVALID) {
            frame_free(tx_data_buffer_frames[index]);
            tx_data_buffer_frames[index] = FRAME_INVALID;
        }
        if (tx_management_buffer_frames[index] != FRAME_INVALID) {
            frame_free(tx_management_buffer_frames[index]);
            tx_management_buffer_frames[index] = FRAME_INVALID;
        }
    }
    if (tx_data_ring_frame != FRAME_INVALID) {
        frame_free(tx_data_ring_frame);
        tx_data_ring_frame = FRAME_INVALID;
    }
    if (tx_management_ring_frame != FRAME_INVALID) {
        frame_free(tx_management_ring_frame);
        tx_management_ring_frame = FRAME_INVALID;
    }
    tx_dma_ready = 0u;
}

static int rtl8192se_prepare_tx_dma(struct rtl8192se_status *status)
{
    if (status == 0 || status->probed == 0u) {
        return 0;
    }
    if (tx_dma_ready != 0u) {
        status->tx_data_ring = tx_data_ring_frame;
        status->tx_management_ring = tx_management_ring_frame;
        status->tx_dma_prepared = 1u;
        return 1;
    }

    for (uint32_t index = 0u; index < RTL8192SE_TX_RING_COUNT; ++index) {
        tx_data_buffer_frames[index] = FRAME_INVALID;
        tx_management_buffer_frames[index] = FRAME_INVALID;
    }
    tx_data_ring_frame = frame_allocate_below(RTL8192SE_DMA_LIMIT);
    tx_management_ring_frame = frame_allocate_below(RTL8192SE_DMA_LIMIT);
    if (tx_data_ring_frame == FRAME_INVALID ||
        tx_management_ring_frame == FRAME_INVALID) {
        release_tx_dma();
        return 0;
    }

    uint8_t *data_ring = (uint8_t *)(uintptr_t)tx_data_ring_frame;
    uint8_t *management_ring =
        (uint8_t *)(uintptr_t)tx_management_ring_frame;
    if (!rtl8192se_build_command_ring(data_ring, tx_data_ring_frame,
                                     RTL8192SE_TX_RING_COUNT) ||
        !rtl8192se_build_command_ring(management_ring,
                                     tx_management_ring_frame,
                                     RTL8192SE_TX_RING_COUNT)) {
        release_tx_dma();
        return 0;
    }

    for (uint32_t index = 0u; index < RTL8192SE_TX_RING_COUNT; ++index) {
        tx_data_buffer_frames[index] =
            frame_allocate_below(RTL8192SE_DMA_LIMIT);
        tx_management_buffer_frames[index] =
            frame_allocate_below(RTL8192SE_DMA_LIMIT);
        if (tx_data_buffer_frames[index] == FRAME_INVALID ||
            tx_management_buffer_frames[index] == FRAME_INVALID) {
            release_tx_dma();
            return 0;
        }
        uint8_t *data_buffer =
            (uint8_t *)(uintptr_t)tx_data_buffer_frames[index];
        uint8_t *management_buffer =
            (uint8_t *)(uintptr_t)tx_management_buffer_frames[index];
        for (uint32_t byte = 0u; byte < RTL8192SE_TX_BUFFER_SIZE; ++byte) {
            data_buffer[byte] = 0u;
            management_buffer[byte] = 0u;
        }
    }

    __asm__ volatile ("" : : : "memory");
    tx_data_index = 0u;
    tx_management_index = 0u;
    tx_dma_ready = 1u;
    status->tx_data_ring = tx_data_ring_frame;
    status->tx_management_ring = tx_management_ring_frame;
    status->tx_dma_prepared = 1u;
    return 1;
}

static uint8_t rtl8192se_mmio_read8(void *context, uint16_t offset)
{
    volatile uint8_t *registers = (volatile uint8_t *)context;
    return registers[offset];
}

static void rtl8192se_mmio_write8(void *context, uint16_t offset,
                                  uint8_t value)
{
    volatile uint8_t *registers = (volatile uint8_t *)context;
    registers[offset] = value;
}

static uint16_t rtl8192se_mmio_read16(void *context, uint16_t offset)
{
    volatile uint16_t *registers =
        (volatile uint16_t *)((volatile uint8_t *)context + offset);
    return *registers;
}

static uint32_t rtl8192se_mmio_read32(void *context, uint16_t offset)
{
    volatile uint32_t *registers =
        (volatile uint32_t *)((volatile uint8_t *)context + offset);
    return *registers;
}

static void rtl8192se_mmio_write16(void *context, uint16_t offset,
                                   uint16_t value)
{
    volatile uint16_t *registers =
        (volatile uint16_t *)((volatile uint8_t *)context + offset);
    *registers = value;
}

static void rtl8192se_mmio_write32(void *context, uint16_t offset,
                                   uint32_t value)
{
    volatile uint32_t *registers =
        (volatile uint32_t *)((volatile uint8_t *)context + offset);
    *registers = value;
}

static void rtl8192se_delay_us(void *context, uint32_t usec)
{
    (void)context;
    for (volatile uint32_t count = 0u; count < usec * 1024u; ++count) {
        __asm__ volatile ("pause");
    }
}

static int rtl8192se_enable_bus_mastering(void *context)
{
    const struct rtl8192se_status *status = context;
    if (status == 0 || status->probed == 0u) {
        return 0;
    }
    if (command_bus_master_enabled != 0u) {
        return 1;
    }
    struct pci_device device = {
        .bus = status->bus,
        .slot = status->slot,
        .function = status->function,
        .vendor_id = RTL8192SE_VENDOR_ID,
        .device_id = status->device_id,
    };
    if (!pci_enable_bus_mastering(&device)) {
        return 0;
    }
    command_bus_master_enabled = 1u;
    return 1;
}

int rtl8192se_start_firmware(struct rtl8192se_status *status,
                             const struct rtl8192se_firmware *firmware)
{
    if (status == 0 || status->probed == 0u || firmware == 0 ||
        status->mmio_base > UINT32_MAX - RTL8192SE_REGISTER_WINDOW_SIZE ||
        !rtl8192se_prepare_command_dma(status) ||
        !rtl8192se_prepare_rx_dma(status) ||
        !rtl8192se_prepare_tx_dma(status)) {
        return 0;
    }
    struct pci_device device = {
        .bus = status->bus,
        .slot = status->slot,
        .function = status->function,
        .vendor_id = RTL8192SE_VENDOR_ID,
        .device_id = status->device_id,
    };
    if (!pci_enable_memory_space(&device)) {
        return 0;
    }
    struct rtl8192se_register_io io = {
        .context = (void *)(uintptr_t)status->mmio_base,
        .bus_context = status,
        .read8 = rtl8192se_mmio_read8,
        .read16 = rtl8192se_mmio_read16,
        .read32 = rtl8192se_mmio_read32,
        .write8 = rtl8192se_mmio_write8,
        .write16 = rtl8192se_mmio_write16,
        .write32 = rtl8192se_mmio_write32,
        .delay_us = rtl8192se_delay_us,
        .enable_bus_mastering = rtl8192se_enable_bus_mastering,
    };
    uint8_t *ring = (uint8_t *)(uintptr_t)command_ring_frame;
    uint8_t *buffer = (uint8_t *)(uintptr_t)command_buffer_frame;
    struct rtl8192se_adapter_config config;
    if (!rtl8192se_mac_power_sequence(
            &io, command_ring_frame, rx_mpdu_ring_frame,
            rx_command_ring_frame, tx_data_ring_frame,
            tx_management_ring_frame)) {
        return 0;
    }
    status->mac_power_ready = 1u;
    if (!rtl8192se_read_adapter_config(&io, &config)) {
        return 0;
    }
    for (uint32_t index = 0u; index < 6u; ++index) {
        status->mac_address[index] = config.mac_address[index];
    }
    status->rf_configuration = config.rf_configuration;
    status->channel_plan = config.channel_plan;
    status->adapter_config_read = 1u;
    if (!rtl8192se_upload_firmware(
            &io, firmware, ring, command_ring_frame,
            RTL8192SE_COMMAND_RING_COUNT, buffer, command_buffer_frame,
            RTL8192SE_PAGE_SIZE, config.rf_configuration,
            &status->firmware_upload_failure,
            &status->firmware_upload_tcr)) {
        return 0;
    }
    status->firmware_uploaded = 1u;
    if (!rtl8192se_initialize_phy(&io, &config)) {
        return 0;
    }
    status->phy_initialized = 1u;
    status->channel = RTL8192SE_SCAN_FIRST_CHANNEL;
    if (!rtl8192se_program_rx_rings(
            &io, rx_mpdu_ring_frame, rx_command_ring_frame)) {
        return 0;
    }
    status->rx_rings_programmed = 1u;
    if (!rtl8192se_program_tx_rings(
            &io, tx_data_ring_frame, tx_management_ring_frame)) {
        return 0;
    }
    status->tx_rings_programmed = 1u;
    if (!rtl8192se_enable_mac_io(&io)) {
        return 0;
    }
    status->mac_io_enabled = 1u;
    adapter_status = *status;
    return 1;
}

int rtl8192se_send_80211_frame(const uint8_t *frame, uint32_t frame_length,
                               int management_frame)
{
    if (frame == 0 || frame_length < 10u ||
        frame_length > RTL8192SE_TX_BUFFER_SIZE ||
        tx_dma_ready == 0u || adapter_status.tx_rings_programmed == 0u ||
        adapter_status.mac_io_enabled == 0u) {
        return 0;
    }

    uint32_t *buffer_frames;
    uint8_t *ring;
    uint32_t *next_index;
    uint8_t queue_selection;
    uint16_t poll_bit;
    uint32_t ring_address;
    if (management_frame != 0) {
        buffer_frames = tx_management_buffer_frames;
        ring_address = tx_management_ring_frame;
        ring = (uint8_t *)(uintptr_t)tx_management_ring_frame;
        next_index = &tx_management_index;
        queue_selection = RTL8192SE_TX_MANAGEMENT_QUEUE_SELECTION;
        poll_bit = RTL8192SE_TX_MANAGEMENT_POLL;
    } else {
        buffer_frames = tx_data_buffer_frames;
        ring_address = tx_data_ring_frame;
        ring = (uint8_t *)(uintptr_t)tx_data_ring_frame;
        next_index = &tx_data_index;
        queue_selection = RTL8192SE_TX_DATA_QUEUE_SELECTION;
        poll_bit = RTL8192SE_TX_DATA_POLL;
    }

    uint32_t index = *next_index;
    uint8_t *descriptor =
        ring + index * RTL8192SE_DESCRIPTOR_SIZE;
    if ((read_le32(descriptor) & (1u << 31)) != 0u) {
        return 0;
    }

    uint8_t *buffer = (uint8_t *)(uintptr_t)buffer_frames[index];
    for (uint32_t byte = 0u; byte < frame_length; ++byte) {
        buffer[byte] = frame[byte];
    }
    uint32_t next_descriptor_address = ring_address +
        ((index + 1u) % RTL8192SE_TX_RING_COUNT) *
            RTL8192SE_DESCRIPTOR_SIZE;
    uint8_t qos_data = frame_length >= 2u &&
        (frame[0] & 0x0cu) == 0x08u && (frame[0] & 0x80u) != 0u;
    if (!rtl8192se_build_tx_descriptor(
            descriptor, buffer_frames[index], frame_length,
            next_descriptor_address, queue_selection,
            (uint8_t)(qos_data == 0u))) {
        return 0;
    }
    if (management_frame != 0) {
        last_management_tx_descriptor = descriptor;
    }

    __asm__ volatile ("" : : : "memory");
    rtl8192se_mmio_write16(
        (void *)(uintptr_t)adapter_status.mmio_base,
        RTL8192SE_TX_POLL_REGISTER, poll_bit);
    *next_index = (index + 1u) % RTL8192SE_TX_RING_COUNT;
    return 1;
}

int rtl8192se_receive_80211_frame(uint8_t *frame, uint32_t frame_capacity,
                                  uint32_t *frame_length)
{
    if (frame == 0 || frame_capacity == 0u || frame_length == 0 ||
        rx_dma_ready == 0u || adapter_status.rx_rings_programmed == 0u ||
        adapter_status.mac_io_enabled == 0u) {
        return 0;
    }

    uint8_t *ring = (uint8_t *)(uintptr_t)rx_mpdu_ring_frame;
    for (uint32_t scanned = 0u; scanned < RTL8192SE_RX_RING_COUNT; ++scanned) {
        uint32_t index = rx_mpdu_index;
        uint8_t *descriptor =
            ring + index * RTL8192SE_RX_DESCRIPTOR_SIZE;
        if ((read_le32(descriptor) & (1u << 31)) != 0u) {
            return 0;
        }

        uint32_t received_length = 0u;
        uint8_t *buffer =
            (uint8_t *)(uintptr_t)rx_mpdu_buffer_frames[index];
        int frame_valid = rtl8192se_rx_copy_frame(
            descriptor, buffer, RTL8192SE_RX_BUFFER_SIZE, frame,
            frame_capacity, &received_length);
        if (!frame_valid &&
            (association_state == RTL8192SE_ASSOCIATION_AUTHENTICATING ||
             association_state == RTL8192SE_ASSOCIATION_ASSOCIATING ||
             association_state == RTL8192SE_ASSOCIATION_HANDSHAKING)) {
            ++association_rx_invalid_descriptors;
        }
        int end_of_ring = index + 1u == RTL8192SE_RX_RING_COUNT;
        if (!rtl8192se_rearm_rx_descriptor(
                descriptor, rx_mpdu_buffer_frames[index],
                RTL8192SE_RX_BUFFER_SIZE, end_of_ring)) {
            return 0;
        }
        rx_mpdu_index = (index + 1u) % RTL8192SE_RX_RING_COUNT;
        if (frame_valid) {
            *frame_length = received_length;
            return 1;
        }
    }
    return 0;
}

static int rtl8192se_valid_bssid(const uint8_t bssid[6])
{
    uint8_t any_nonzero = 0u;
    uint8_t all_ones = 0xffu;
    for (uint32_t index = 0u; index < 6u; ++index) {
        any_nonzero |= bssid[index];
        all_ones &= bssid[index];
    }
    return any_nonzero != 0u && all_ones != 0xffu &&
           (bssid[0] & 1u) == 0u;
}

static int rtl8192se_mac_equal(const uint8_t left[6],
                               const uint8_t right[6])
{
    for (uint32_t index = 0u; index < 6u; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

static int rtl8192se_extract_eapol(const uint8_t *frame, uint32_t frame_length,
                                   const uint8_t bssid[6],
                                   const uint8_t station_address[6],
                                   const uint8_t **eapol,
                                   uint32_t *eapol_length)
{
    if (frame == 0 || bssid == 0 || station_address == 0 || eapol == 0 ||
        eapol_length == 0 || frame_length < 32u ||
        (frame[0] & 0x0cu) != 0x08u || (frame[0] & 0x03u) != 0u ||
        (((frame[0] >> 4) & 0x0fu) != 0u &&
         ((frame[0] >> 4) & 0x0fu) != 8u) ||
        (frame[1] & 0x03u) != 0x02u || (frame[1] & 0x44u) != 0u ||
        (frame[1] & 0x80u) != 0u ||
        !rtl8192se_mac_equal(frame + 4u, station_address) ||
        !rtl8192se_mac_equal(frame + 10u, bssid) ||
        !rtl8192se_mac_equal(frame + 16u, bssid)) {
        return 0;
    }
    uint32_t header_length =
        ((frame[0] >> 4) & 0x0fu) == 8u ? 26u : 24u;
    if (frame_length < header_length + RTL8192SE_EAPOL_SNAP_LENGTH ||
        frame[header_length] != 0xaau ||
        frame[header_length + 1u] != 0xaau ||
        frame[header_length + 2u] != 0x03u ||
        frame[header_length + 3u] != 0u ||
        frame[header_length + 4u] != 0u ||
        frame[header_length + 5u] != 0u ||
        frame[header_length + 6u] != 0x88u ||
        frame[header_length + 7u] != 0x8eu) {
        return 0;
    }
    *eapol = frame + header_length + RTL8192SE_EAPOL_SNAP_LENGTH;
    *eapol_length =
        frame_length - header_length - RTL8192SE_EAPOL_SNAP_LENGTH;
    return *eapol_length >= 4u;
}

static int rtl8192se_send_eapol(const uint8_t *eapol,
                                uint32_t eapol_length)
{
    if (eapol == 0 || eapol_length == 0u ||
        eapol_length > WPA2_EAPOL_MAX_SIZE ||
        RTL8192SE_80211_FRAME_MAX <
            24u + RTL8192SE_EAPOL_SNAP_LENGTH + eapol_length) {
        return 0;
    }
    wireless_frame_buffer[0] = 0x08u;
    wireless_frame_buffer[1] = 0x01u;
    wireless_frame_buffer[2] = 0u;
    wireless_frame_buffer[3] = 0u;
    for (uint32_t index = 0u; index < 6u; ++index) {
        wireless_frame_buffer[4u + index] = associated_bssid[index];
        wireless_frame_buffer[10u + index] = adapter_status.mac_address[index];
        wireless_frame_buffer[16u + index] = associated_bssid[index];
    }
    wireless_frame_buffer[22] =
        (uint8_t)(data_sequence_number << 4);
    wireless_frame_buffer[23] = (uint8_t)(data_sequence_number >> 4);
    static const uint8_t llc_snap[RTL8192SE_EAPOL_SNAP_LENGTH] = {
        0xaau, 0xaau, 0x03u, 0u, 0u, 0u, 0x88u, 0x8eu,
    };
    for (uint32_t index = 0u; index < sizeof(llc_snap); ++index) {
        wireless_frame_buffer[24u + index] = llc_snap[index];
    }
    for (uint32_t index = 0u; index < eapol_length; ++index) {
        wireless_frame_buffer[32u + index] = eapol[index];
    }
    if (!rtl8192se_send_80211_frame(
            wireless_frame_buffer, 32u + eapol_length, 0)) {
        return 0;
    }
    data_sequence_number = (uint16_t)((data_sequence_number + 1u) & 0x0fffu);
    return 1;
}

static enum wpa2_handshake_result rtl8192se_process_eapol_frame(
    const uint8_t *frame, uint32_t frame_length)
{
    const uint8_t *eapol;
    uint32_t eapol_length;
    uint8_t response[WPA2_EAPOL_MAX_SIZE];
    uint32_t response_length = 0u;
    if (!rtl8192se_extract_eapol(frame, frame_length, associated_bssid,
                                 adapter_status.mac_address, &eapol,
                                 &eapol_length)) {
        return WPA2_HANDSHAKE_IGNORE;
    }
    uint32_t previous_gtk_generation = wpa2_session.gtk_generation;
    uint32_t previous_ptk_generation = wpa2_session.ptk_generation;
    enum wpa2_handshake_result result = wpa2_handshake_process(
        &wpa2_session, eapol, eapol_length, response, sizeof(response),
        &response_length);
    if (wpa2_session.gtk_generation != previous_gtk_generation) {
        group_tx_packet_number = 0u;
        group_rx_packet_number = 0u;
    }
    if (wpa2_session.ptk_generation != previous_ptk_generation) {
        pairwise_tx_packet_number = 0u;
        pairwise_rx_packet_number = 0u;
    }
    if ((result == WPA2_HANDSHAKE_RESPONSE ||
         result == WPA2_HANDSHAKE_COMPLETE) &&
        !rtl8192se_send_eapol(response, response_length)) {
        result = WPA2_HANDSHAKE_ERROR;
        rtl8192se_mark_disconnected();
    }
    volatile uint8_t *secret_response = response;
    for (uint32_t index = 0u; index < sizeof(response); ++index) {
        secret_response[index] = 0u;
    }
    return result;
}

static int rtl8192se_mark_open_associated(const uint8_t bssid[6])
{
    if (bssid == 0 || !rtl8192se_valid_bssid(bssid) ||
        adapter_status.firmware_uploaded == 0u ||
        adapter_status.mac_io_enabled == 0u) {
        return 0;
    }
    for (uint32_t index = 0u; index < 6u; ++index) {
        associated_bssid[index] = bssid[index];
    }
    data_sequence_number = 0u;
    wifi_associated = 1u;
    association_security_wpa2 = 0u;
    association_state = RTL8192SE_ASSOCIATION_CONNECTED_OPEN;
    return 1;
}

static int rtl8192se_mark_wpa2_associated(const uint8_t bssid[6])
{
    if (bssid == 0 || !rtl8192se_valid_bssid(bssid) ||
        adapter_status.firmware_uploaded == 0u ||
        adapter_status.mac_io_enabled == 0u ||
        wpa2_session.state != WPA2_HANDSHAKE_ESTABLISHED ||
        wpa2_session.gtk_valid == 0u) {
        return 0;
    }
    for (uint32_t index = 0u; index < 6u; ++index) {
        associated_bssid[index] = bssid[index];
    }
    pairwise_tx_packet_number = 0u;
    group_tx_packet_number = 0u;
    pairwise_rx_packet_number = 0u;
    group_rx_packet_number = 0u;
    association_security_wpa2 = 1u;
    wifi_associated = 1u;
    association_state = RTL8192SE_ASSOCIATION_CONNECTED_WPA2;
    return 1;
}

void rtl8192se_mark_disconnected(void)
{
    wifi_associated = 0u;
    association_security_wpa2 = 0u;
    association_ssid_length = 0u;
    association_state = RTL8192SE_ASSOCIATION_IDLE;
    association_failure = RTL8192SE_ASSOCIATION_FAILURE_NONE;
    last_management_tx_descriptor = 0;
    wpa2_handshake_clear(&wpa2_session);
    pairwise_tx_packet_number = 0u;
    group_tx_packet_number = 0u;
    pairwise_rx_packet_number = 0u;
    group_rx_packet_number = 0u;
    for (uint32_t index = 0u; index < sizeof(associated_bssid); ++index) {
        associated_bssid[index] = 0u;
    }
    for (uint32_t index = 0u; index < sizeof(association_ssid); ++index) {
        association_ssid[index] = 0u;
    }
}

int rtl8192se_open_association_start(
    const struct rtl8192se_scan_result *network)
{
    uint32_t frame_length = 0u;
    if (network == 0 ||
        network->security != RTL8192SE_SECURITY_OPEN ||
        network->ssid_length == 0u || network->ssid_length > 32u ||
        network->channel == 0u || network->channel > 14u ||
        !rtl8192se_valid_bssid(network->bssid) ||
        adapter_status.firmware_uploaded == 0u ||
        adapter_status.phy_initialized == 0u ||
        adapter_status.mac_io_enabled == 0u ||
        !rtl8192se_valid_bssid(adapter_status.mac_address)) {
        return 0;
    }

    rtl8192se_mark_disconnected();
    rtl8192se_reset_association_diagnostics();
    if (!rtl8192se_set_current_channel(network->channel) ||
        !rtl8192se_build_open_auth_request(
            network->bssid, adapter_status.mac_address,
            management_sequence_number, wireless_frame_buffer,
            sizeof(wireless_frame_buffer), &frame_length) ||
        !rtl8192se_send_80211_frame(wireless_frame_buffer, frame_length, 1)) {
        association_state = RTL8192SE_ASSOCIATION_FAILED;
        return 0;
    }
    rtl8192se_record_auth_tx_submission();
    association_request_sequence = management_sequence_number;
    for (uint32_t index = 0u; index < 6u; ++index) {
        associated_bssid[index] = network->bssid[index];
    }
    for (uint32_t index = 0u; index < network->ssid_length; ++index) {
        association_ssid[index] = network->ssid[index];
    }
    association_ssid_length = network->ssid_length;
    management_sequence_number =
        (uint16_t)((management_sequence_number + 1u) & 0x0fffu);
    association_deadline =
        pit_ticks_read() + RTL8192SE_ASSOCIATION_TIMEOUT_TICKS;
    association_retry_count = 0u;
    association_retry_deadline =
        pit_ticks_read() + RTL8192SE_AUTH_RETRY_INTERVAL_TICKS;
    association_state = RTL8192SE_ASSOCIATION_AUTHENTICATING;
    return 1;
}

int rtl8192se_wpa2_association_start(
    const struct rtl8192se_scan_result *network,
    const uint8_t *passphrase, uint32_t passphrase_length)
{
    uint32_t frame_length = 0u;
    if (network == 0 ||
        network->security != RTL8192SE_SECURITY_WPA2_PSK_CCMP ||
        network->ssid_length == 0u || network->ssid_length > 32u ||
        network->channel == 0u || network->channel > 13u ||
        !rtl8192se_valid_bssid(network->bssid) ||
        adapter_status.firmware_uploaded == 0u ||
        adapter_status.phy_initialized == 0u ||
        adapter_status.mac_io_enabled == 0u ||
        !rtl8192se_valid_bssid(adapter_status.mac_address)) {
        return 0;
    }

    rtl8192se_mark_disconnected();
    rtl8192se_reset_association_diagnostics();
    if (!wpa2_handshake_begin(
            &wpa2_session, passphrase, passphrase_length,
            network->ssid, network->ssid_length, network->bssid,
            adapter_status.mac_address, wpa2_secure_random_fill, 0) ||
        !rtl8192se_set_current_channel(network->channel) ||
        !rtl8192se_build_open_auth_request(
            network->bssid, adapter_status.mac_address,
            management_sequence_number, wireless_frame_buffer,
            sizeof(wireless_frame_buffer), &frame_length) ||
        !rtl8192se_send_80211_frame(wireless_frame_buffer, frame_length, 1)) {
        wpa2_handshake_clear(&wpa2_session);
        association_state = RTL8192SE_ASSOCIATION_FAILED;
        return 0;
    }
    rtl8192se_record_auth_tx_submission();
    association_request_sequence = management_sequence_number;
    for (uint32_t index = 0u; index < 6u; ++index) {
        associated_bssid[index] = network->bssid[index];
    }
    for (uint32_t index = 0u; index < network->ssid_length; ++index) {
        association_ssid[index] = network->ssid[index];
    }
    association_ssid_length = network->ssid_length;
    association_security_wpa2 = 1u;
    management_sequence_number =
        (uint16_t)((management_sequence_number + 1u) & 0x0fffu);
    association_deadline =
        pit_ticks_read() + RTL8192SE_ASSOCIATION_TIMEOUT_TICKS;
    association_retry_count = 0u;
    association_retry_deadline =
        pit_ticks_read() + RTL8192SE_AUTH_RETRY_INTERVAL_TICKS;
    association_state = RTL8192SE_ASSOCIATION_AUTHENTICATING;
    return 1;
}

enum rtl8192se_association_state rtl8192se_open_association_poll(void)
{
    if (association_state != RTL8192SE_ASSOCIATION_AUTHENTICATING &&
        association_state != RTL8192SE_ASSOCIATION_ASSOCIATING &&
        association_state != RTL8192SE_ASSOCIATION_HANDSHAKING) {
        return association_state;
    }
    if (association_state == RTL8192SE_ASSOCIATION_AUTHENTICATING) {
        (void)rtl8192se_update_auth_tx_completions();
    }
    if ((int32_t)(pit_ticks_read() - association_deadline) >= 0) {
        enum rtl8192se_association_failure failure =
            association_state == RTL8192SE_ASSOCIATION_AUTHENTICATING
                ? RTL8192SE_ASSOCIATION_FAILURE_AUTH_TIMEOUT
            : association_state == RTL8192SE_ASSOCIATION_ASSOCIATING
                ? RTL8192SE_ASSOCIATION_FAILURE_ASSOC_TIMEOUT
                : RTL8192SE_ASSOCIATION_FAILURE_WPA2_TIMEOUT;
        if (association_state == RTL8192SE_ASSOCIATION_AUTHENTICATING &&
            rtl8192se_update_auth_tx_completions()) {
            failure = RTL8192SE_ASSOCIATION_FAILURE_TX_STUCK;
        }
        rtl8192se_mark_disconnected();
        association_failure = failure;
        association_state = RTL8192SE_ASSOCIATION_FAILED;
        return association_state;
    }

    if (association_state == RTL8192SE_ASSOCIATION_AUTHENTICATING &&
        association_retry_count < RTL8192SE_AUTH_RETRY_LIMIT &&
        (int32_t)(pit_ticks_read() - association_retry_deadline) >= 0) {
        uint32_t retry_length = 0u;
        if (!rtl8192se_build_open_auth_request(
                associated_bssid, adapter_status.mac_address,
                association_request_sequence, wireless_frame_buffer,
                sizeof(wireless_frame_buffer), &retry_length)) {
            rtl8192se_mark_disconnected();
            association_failure = RTL8192SE_ASSOCIATION_FAILURE_TX;
            association_state = RTL8192SE_ASSOCIATION_FAILED;
            return association_state;
        }
        wireless_frame_buffer[1] |= 0x08u;
        if (!rtl8192se_send_80211_frame(
                wireless_frame_buffer, retry_length, 1)) {
            rtl8192se_mark_disconnected();
            association_failure = RTL8192SE_ASSOCIATION_FAILURE_TX;
            association_state = RTL8192SE_ASSOCIATION_FAILED;
            return association_state;
        }
        rtl8192se_record_auth_tx_submission();
        ++association_retry_count;
        association_retry_deadline += RTL8192SE_AUTH_RETRY_INTERVAL_TICKS;
    }

    uint32_t frame_length = 0u;
    uint8_t accepted = 0u;
    if (association_state == RTL8192SE_ASSOCIATION_HANDSHAKING) {
        if (!rtl8192se_receive_80211_frame(
                wireless_frame_buffer, sizeof(wireless_frame_buffer),
                &frame_length)) {
            return association_state;
        }
        enum wpa2_handshake_result result =
            rtl8192se_process_eapol_frame(wireless_frame_buffer,
                                          frame_length);
        if (result == WPA2_HANDSHAKE_IGNORE) {
            return association_state;
        }
        if (result == WPA2_HANDSHAKE_COMPLETE &&
            rtl8192se_mark_wpa2_associated(associated_bssid)) {
            return association_state;
        }
        if (result == WPA2_HANDSHAKE_ERROR ||
            result == WPA2_HANDSHAKE_COMPLETE) {
            rtl8192se_mark_disconnected();
            association_failure = RTL8192SE_ASSOCIATION_FAILURE_WPA2_HANDSHAKE;
            association_state = RTL8192SE_ASSOCIATION_FAILED;
        }
        return association_state;
    }
    if (!rtl8192se_receive_80211_frame(
            wireless_frame_buffer, sizeof(wireless_frame_buffer),
            &frame_length)) {
        return association_state;
    }
    ++association_rx_frames;
    if (association_state == RTL8192SE_ASSOCIATION_AUTHENTICATING) {
        if (frame_length >= 2u &&
            (wireless_frame_buffer[0] & 0xfcu) == 0xb0u) {
            ++association_auth_responses;
        }
        if (!rtl8192se_parse_open_auth_response(
                wireless_frame_buffer, frame_length, associated_bssid,
                adapter_status.mac_address,
                &accepted)) {
            return association_state;
        }
        if (accepted == 0u) {
            association_failure =
                RTL8192SE_ASSOCIATION_FAILURE_AUTH_REJECTED;
            rtl8192se_mark_disconnected();
            association_failure =
                RTL8192SE_ASSOCIATION_FAILURE_AUTH_REJECTED;
            association_state = RTL8192SE_ASSOCIATION_FAILED;
            return association_state;
        }
        uint32_t association_length = 0u;
        int request_built = association_security_wpa2 != 0u
            ? rtl8192se_build_wpa2_assoc_request(
                  associated_bssid, adapter_status.mac_address,
                  association_ssid, association_ssid_length,
                  management_sequence_number, wireless_frame_buffer,
                  sizeof(wireless_frame_buffer), &association_length)
            : rtl8192se_build_open_assoc_request(
                  associated_bssid, adapter_status.mac_address,
                  association_ssid, association_ssid_length,
                  management_sequence_number, wireless_frame_buffer,
                  sizeof(wireless_frame_buffer), &association_length);
        if (!request_built ||
            !rtl8192se_send_80211_frame(wireless_frame_buffer,
                                        association_length, 1)) {
            association_failure = RTL8192SE_ASSOCIATION_FAILURE_TX;
            rtl8192se_mark_disconnected();
            association_failure = RTL8192SE_ASSOCIATION_FAILURE_TX;
            association_state = RTL8192SE_ASSOCIATION_FAILED;
            return association_state;
        }
        management_sequence_number =
            (uint16_t)((management_sequence_number + 1u) & 0x0fffu);
        association_state = RTL8192SE_ASSOCIATION_ASSOCIATING;
        return association_state;
    }

    int response_parsed = association_security_wpa2 != 0u
        ? rtl8192se_parse_wpa2_assoc_response(
              wireless_frame_buffer, frame_length, associated_bssid,
              adapter_status.mac_address, &accepted)
        : rtl8192se_parse_open_assoc_response(
              wireless_frame_buffer, frame_length, associated_bssid,
              adapter_status.mac_address, &accepted);
    if (!response_parsed) {
        return association_state;
    }
    if (accepted == 0u ||
        (association_security_wpa2 == 0u &&
         !rtl8192se_mark_open_associated(associated_bssid))) {
        association_failure =
            RTL8192SE_ASSOCIATION_FAILURE_ASSOC_REJECTED;
        rtl8192se_mark_disconnected();
        association_failure =
            RTL8192SE_ASSOCIATION_FAILURE_ASSOC_REJECTED;
        association_state = RTL8192SE_ASSOCIATION_FAILED;
        return association_state;
    }
    if (association_security_wpa2 != 0u) {
        association_state = RTL8192SE_ASSOCIATION_HANDSHAKING;
        return association_state;
    }
    return RTL8192SE_ASSOCIATION_CONNECTED_OPEN;
}

enum rtl8192se_association_failure
rtl8192se_association_last_failure(void)
{
    return association_failure;
}

uint32_t rtl8192se_association_rx_frame_count(void)
{
    return association_rx_frames;
}

uint32_t rtl8192se_association_auth_response_count(void)
{
    return association_auth_responses;
}

uint32_t rtl8192se_association_auth_tx_submission_count(void)
{
    return association_auth_tx_submissions;
}

uint32_t rtl8192se_association_auth_tx_dma_completion_count(void)
{
    return association_auth_tx_dma_completions;
}

uint32_t rtl8192se_association_rx_invalid_descriptor_count(void)
{
    return association_rx_invalid_descriptors;
}

static int rtl8192se_set_current_channel(uint8_t channel)
{
    struct rtl8192se_register_io io = {
        .context = (void *)(uintptr_t)adapter_status.mmio_base,
        .bus_context = &adapter_status,
        .read8 = rtl8192se_mmio_read8,
        .read16 = rtl8192se_mmio_read16,
        .read32 = rtl8192se_mmio_read32,
        .write8 = rtl8192se_mmio_write8,
        .write16 = rtl8192se_mmio_write16,
        .write32 = rtl8192se_mmio_write32,
        .delay_us = rtl8192se_delay_us,
    };
    if (!rtl8192se_set_channel(&io, channel)) {
        return 0;
    }
    adapter_status.channel = channel;
    return 1;
}

static int rtl8192se_send_probe_request(void)
{
    uint32_t frame_length = 0u;
    int built = scan_target_ssid_length == 0u
        ? rtl8192se_build_probe_request(
              adapter_status.mac_address, management_sequence_number,
              wireless_frame_buffer, sizeof(wireless_frame_buffer),
              &frame_length)
        : rtl8192se_build_probe_request_for_ssid(
              adapter_status.mac_address, scan_target_ssid,
              scan_target_ssid_length, management_sequence_number,
              wireless_frame_buffer, sizeof(wireless_frame_buffer),
              &frame_length);
    if (!built ||
        !rtl8192se_send_80211_frame(wireless_frame_buffer, frame_length, 1)) {
        return 0;
    }
    management_sequence_number =
        (uint16_t)((management_sequence_number + 1u) & 0x0fffu);
    return 1;
}

static int rtl8192se_scan_start_for_ssid(const uint8_t *ssid,
                                         uint8_t ssid_length)
{
    if (wifi_associated != 0u ||
        association_state == RTL8192SE_ASSOCIATION_AUTHENTICATING ||
        association_state == RTL8192SE_ASSOCIATION_ASSOCIATING ||
        adapter_status.firmware_uploaded == 0u ||
        adapter_status.phy_initialized == 0u ||
        adapter_status.mac_io_enabled == 0u ||
        adapter_status.rx_rings_programmed == 0u ||
        adapter_status.tx_rings_programmed == 0u) {
        return 0;
    }
    if ((ssid_length != 0u && ssid == 0) || ssid_length > 32u) {
        return 0;
    }

    scan_result_count = 0u;
    scan_received_frame_count = 0u;
    scan_parsed_frame_count = 0u;
    scan_management_frame_count = 0u;
    scan_beacon_frame_count = 0u;
    scan_probe_response_frame_count = 0u;
    scan_other_frame_count = 0u;
    scan_short_frame_count = 0u;
    for (uint32_t index = 0u; index < RTL8192SE_SCAN_RESULT_MAX; ++index) {
        scan_results[index] = (struct rtl8192se_scan_result){0};
    }
    scan_target_ssid_length = ssid_length;
    for (uint32_t index = 0u; index < ssid_length; ++index) {
        scan_target_ssid[index] = ssid[index];
    }
    scan_channel = RTL8192SE_SCAN_FIRST_CHANNEL;
    if (!rtl8192se_set_current_channel(scan_channel) ||
        !rtl8192se_send_probe_request()) {
        scan_state = RTL8192SE_SCAN_FAILED;
        return 0;
    }
    scan_deadline = pit_ticks_read() + RTL8192SE_SCAN_DWELL_TICKS;
    scan_state = RTL8192SE_SCAN_ACTIVE;
    return 1;
}

int rtl8192se_scan_start(void)
{
    return rtl8192se_scan_start_for_ssid(0, 0u);
}

int rtl8192se_scan_for_ssid_start(const uint8_t *ssid, uint8_t ssid_length)
{
    if (ssid == 0 || ssid_length == 0u) {
        return 0;
    }
    return rtl8192se_scan_start_for_ssid(ssid, ssid_length);
}

enum rtl8192se_scan_state rtl8192se_scan_poll(void)
{
    if (scan_state != RTL8192SE_SCAN_ACTIVE) {
        return scan_state;
    }
    for (uint32_t attempt = 0u; attempt < RTL8192SE_RX_RING_COUNT; ++attempt) {
        uint32_t frame_length = 0u;
        if (!rtl8192se_receive_80211_frame(
                wireless_frame_buffer, sizeof(wireless_frame_buffer),
                &frame_length)) {
            break;
        }
        ++scan_received_frame_count;
        if (frame_length < 2u) {
            ++scan_short_frame_count;
        } else if ((wireless_frame_buffer[0] & 0x0cu) == 0u) {
            ++scan_management_frame_count;
            uint8_t subtype = wireless_frame_buffer[0] >> 4;
            if (subtype == 8u) {
                ++scan_beacon_frame_count;
            } else if (subtype == 5u) {
                ++scan_probe_response_frame_count;
            }
        } else {
            ++scan_other_frame_count;
        }
        struct rtl8192se_scan_result result;
        if (!rtl8192se_parse_scan_frame(wireless_frame_buffer, frame_length,
                                        &result)) {
            continue;
        }
        ++scan_parsed_frame_count;
        if (scan_target_ssid_length != 0u) {
            if (result.ssid_length == 0u) {
                if ((wireless_frame_buffer[0] >> 4) != 5u) {
                    continue;
                }
                result.ssid_length = scan_target_ssid_length;
                for (uint32_t index = 0u;
                     index < scan_target_ssid_length; ++index) {
                    result.ssid[index] = scan_target_ssid[index];
                }
            } else if (result.ssid_length != scan_target_ssid_length) {
                continue;
            }
            uint8_t ssid_matches_target = 1u;
            for (uint32_t index = 0u;
                 index < scan_target_ssid_length; ++index) {
                if (result.ssid[index] != scan_target_ssid[index]) {
                    ssid_matches_target = 0u;
                    break;
                }
            }
            if (ssid_matches_target == 0u) {
                continue;
            }
        }
        if (result.channel != 0u && result.channel != scan_channel) {
            continue;
        }
        if (result.channel == 0u) {
            result.channel = scan_channel;
        }

        uint32_t result_index = 0u;
        while (result_index < scan_result_count &&
               !rtl8192se_mac_equal(scan_results[result_index].bssid,
                                    result.bssid)) {
            ++result_index;
        }
        if (result_index == scan_result_count) {
            if (scan_result_count == RTL8192SE_SCAN_RESULT_MAX) {
                continue;
            }
            ++scan_result_count;
        }
        scan_results[result_index] = result;
    }

    if ((int32_t)(pit_ticks_read() - scan_deadline) >= 0) {
        if (scan_channel == RTL8192SE_SCAN_LAST_CHANNEL) {
            scan_state = RTL8192SE_SCAN_COMPLETE;
            return scan_state;
        }
        ++scan_channel;
        if (!rtl8192se_set_current_channel(scan_channel) ||
            !rtl8192se_send_probe_request()) {
            scan_state = RTL8192SE_SCAN_FAILED;
            return scan_state;
        }
        scan_deadline = pit_ticks_read() + RTL8192SE_SCAN_DWELL_TICKS;
    }
    return scan_state;
}

uint32_t rtl8192se_scan_result_count(void)
{
    return scan_result_count;
}

uint32_t rtl8192se_scan_received_frame_count(void)
{
    return scan_received_frame_count;
}

uint32_t rtl8192se_scan_parsed_frame_count(void)
{
    return scan_parsed_frame_count;
}

uint32_t rtl8192se_scan_management_frame_count(void)
{
    return scan_management_frame_count;
}

uint32_t rtl8192se_scan_beacon_frame_count(void)
{
    return scan_beacon_frame_count;
}

uint32_t rtl8192se_scan_probe_response_frame_count(void)
{
    return scan_probe_response_frame_count;
}

uint32_t rtl8192se_scan_other_frame_count(void)
{
    return scan_other_frame_count;
}

uint32_t rtl8192se_scan_short_frame_count(void)
{
    return scan_short_frame_count;
}

int rtl8192se_scan_get_result(uint32_t index,
                              struct rtl8192se_scan_result *result)
{
    if (result == 0 || index >= scan_result_count) {
        return 0;
    }
    *result = scan_results[index];
    return 1;
}

int rtl8192se_network_is_associated(void)
{
    return wifi_associated != 0u &&
           adapter_status.firmware_uploaded != 0u &&
           adapter_status.phy_initialized != 0u &&
           adapter_status.mac_io_enabled != 0u;
}

int rtl8192se_network_is_wpa2(void)
{
    return rtl8192se_network_is_associated() &&
           association_security_wpa2 != 0u;
}

int rtl8192se_network_send(const void *frame, uint16_t length)
{
    if (!rtl8192se_network_is_associated() || frame == 0 ||
        length > RTL8192SE_ETHERNET_FRAME_MAX) {
        return 0;
    }
    uint32_t wireless_length = 0u;
    if (!rtl8192se_encapsulate_ethernet(
            frame, length, associated_bssid, adapter_status.mac_address,
            data_sequence_number, wireless_frame_buffer,
            sizeof(wireless_frame_buffer), &wireless_length)) {
        return 0;
    }
    if (association_security_wpa2 != 0u) {
        const uint8_t *ethernet = (const uint8_t *)frame;
        uint8_t group_frame = (uint8_t)(ethernet[0] & 1u);
        const uint8_t *key = group_frame != 0u
            ? wpa2_session.gtk
            : wpa2_session.ptk + 32u;
        uint8_t key_id = group_frame != 0u ? wpa2_session.gtk_id : 0u;
        uint64_t *packet_number = group_frame != 0u
            ? &group_tx_packet_number
            : &pairwise_tx_packet_number;
        if (!wpa2_ccmp_protect_frame(
                wireless_frame_buffer, &wireless_length,
                sizeof(wireless_frame_buffer), key, key_id, packet_number)) {
            return 0;
        }
    }
    if (!rtl8192se_send_80211_frame(wireless_frame_buffer, wireless_length,
                                    0)) {
        return 0;
    }
    data_sequence_number = (uint16_t)((data_sequence_number + 1u) & 0x0fffu);
    return 1;
}

int rtl8192se_network_receive(void *frame, uint16_t capacity,
                              uint16_t *length)
{
    if (!rtl8192se_network_is_associated() || frame == 0 || length == 0 ||
        capacity > sizeof(ethernet_frame_buffer)) {
        return 0;
    }
    for (uint32_t attempt = 0u; attempt < RTL8192SE_RX_RING_COUNT; ++attempt) {
        uint32_t wireless_length = 0u;
        if (!rtl8192se_receive_80211_frame(
                wireless_frame_buffer, sizeof(wireless_frame_buffer),
                &wireless_length)) {
            return 0;
        }
        if (association_security_wpa2 != 0u) {
            if ((wireless_frame_buffer[1] & 0x40u) == 0u) {
                (void)rtl8192se_process_eapol_frame(
                    wireless_frame_buffer, wireless_length);
                continue;
            }
            if (!wpa2_ccmp_unprotect_frame(
                    wireless_frame_buffer, &wireless_length,
                    sizeof(wireless_frame_buffer), wpa2_session.ptk + 32u,
                    wpa2_session.gtk, wpa2_session.gtk_id,
                    &pairwise_rx_packet_number, &group_rx_packet_number)) {
                continue;
            }
        }
        uint32_t ethernet_length = 0u;
        if (!rtl8192se_decapsulate_ethernet(
                wireless_frame_buffer, wireless_length, associated_bssid,
                adapter_status.mac_address, ethernet_frame_buffer,
                sizeof(ethernet_frame_buffer), &ethernet_length) ||
            ethernet_length > capacity) {
            continue;
        }
        for (uint32_t index = 0u; index < ethernet_length; ++index) {
            ((uint8_t *)frame)[index] = ethernet_frame_buffer[index];
        }
        *length = (uint16_t)ethernet_length;
        return 1;
    }
    return 0;
}

void rtl8192se_network_get_mac(uint8_t address[6])
{
    if (address == 0) {
        return;
    }
    for (uint32_t index = 0u; index < 6u; ++index) {
        address[index] = adapter_status.mac_address[index];
    }
}

static int rtl8192se_device_supported(uint16_t device_id)
{
    return device_id == 0x8192u ||
           (device_id >= 0x8171u && device_id <= 0x8174u);
}

int rtl8192se_probe(struct rtl8192se_status *status)
{
    rtl8192se_mark_disconnected();
    scan_state = RTL8192SE_SCAN_IDLE;
    scan_result_count = 0u;
    scan_target_ssid_length = 0u;
    adapter_status = (struct rtl8192se_status){0};
    struct pci_device device;
    for (uint32_t index = 0u; index < pci_device_count(); ++index) {
        if (!pci_get_device(index, &device) ||
            device.vendor_id != RTL8192SE_VENDOR_ID ||
            !rtl8192se_device_supported(device.device_id)) {
            continue;
        }

        struct pci_bar bar;
        if (!pci_read_bar(&device, RTL8192SE_REGISTER_BAR, &bar) ||
            bar.is_io != 0u || bar.address > UINT32_MAX ||
            bar.address > UINT32_MAX - RTL8192SE_REGISTER_WINDOW_SIZE) {
            break;
        }

        uint32_t base = (uint32_t)bar.address;
        if (!paging_map_mmio(base, RTL8192SE_REGISTER_WINDOW_SIZE)) {
            break;
        }

        adapter_status.device_id = device.device_id;
        adapter_status.mmio_base = base;
        adapter_status.bus = device.bus;
        adapter_status.slot = device.slot;
        adapter_status.function = device.function;
        adapter_status.probed = 1u;
        break;
    }

    if (status != 0) {
        *status = adapter_status;
    }
    return adapter_status.probed != 0u;
}
