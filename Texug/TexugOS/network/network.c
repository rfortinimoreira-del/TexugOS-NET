#include <stdint.h>

#include "link.h"
#include "network.h"
#include "pit.h"

#define ETHERNET_HEADER_SIZE 14u
#define IPV4_HEADER_SIZE 20u
#define UDP_HEADER_SIZE 8u
#define DHCP_FIXED_SIZE 240u
#define DHCP_MAX_SIZE 320u
#define PACKET_CAPACITY 1600u
#define DHCP_TIMEOUT_TICKS 150u
#define PING_TIMEOUT_TICKS 200u
#define DNS_TIMEOUT_TICKS 300u
#define TCP_TIMEOUT_TICKS 500u
#define DHCP_TRANSACTION_ID 0x54584731u
#define ICMP_IDENTIFIER 0x5447u
#define DNS_QUERY_CAPACITY 512u

#define ETHERNET_IPV4 0x0800u
#define ETHERNET_ARP 0x0806u
#define IPV4_UDP 17u
#define IPV4_ICMP 1u

struct dhcp_reply {
    uint8_t message_type;
    uint8_t address[4];
    uint8_t subnet_mask[4];
    uint8_t gateway[4];
    uint8_t dns_server[4];
    uint8_t server[4];
};

static struct network_config configuration;
static uint8_t hardware_address[6];
static uint8_t transmit_packet[PACKET_CAPACITY];
static uint8_t dhcp_payload[DHCP_MAX_SIZE];
static uint8_t dns_payload[DNS_QUERY_CAPACITY];
static uint8_t receive_packet[PACKET_CAPACITY];
static uint16_t ipv4_identifier;
static uint16_t dns_identifier;
static uint8_t network_available;
static const char *dhcp_error;
static const struct network_link *active_link;

struct tcp_segment {
    uint32_t sequence;
    uint32_t acknowledgment;
    uint8_t flags;
    const uint8_t *payload;
    uint16_t payload_length;
};

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static void write_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void write_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       uint32_t length)
{
    for (uint32_t index = 0u; index < length; ++index) {
        destination[index] = source[index];
    }
}

static int bytes_equal(const uint8_t *left, const uint8_t *right,
                       uint32_t length)
{
    for (uint32_t index = 0u; index < length; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

static uint16_t internet_checksum(const uint8_t *data, uint32_t length)
{
    uint32_t sum = 0u;
    while (length >= 2u) {
        sum += read_be16(data);
        data += 2;
        length -= 2u;
    }
    if (length != 0u) {
        sum += (uint16_t)data[0] << 8;
    }
    while ((sum >> 16) != 0u) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static uint16_t tcp_checksum(const uint8_t *segment, uint16_t length,
                             const uint8_t destination[4])
{
    uint32_t sum = 0u;
    for (uint32_t index = 0u; index < 4u; index += 2u) {
        sum += read_be16(configuration.address + index);
        sum += read_be16(destination + index);
    }
    sum += 6u;
    sum += length;

    while (length >= 2u) {
        sum += read_be16(segment);
        segment += 2u;
        length = (uint16_t)(length - 2u);
    }
    if (length != 0u) {
        sum += (uint16_t)segment[0] << 8;
    }
    while ((sum >> 16) != 0u) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static int append_text(char *buffer, uint32_t capacity, uint32_t *length,
                       const char *text)
{
    while (*text != '\0') {
        if (*length + 1u >= capacity) {
            return 0;
        }
        buffer[(*length)++] = *text++;
    }
    buffer[*length] = '\0';
    return 1;
}

static int append_decimal(char *buffer, uint32_t capacity, uint32_t *length,
                          uint16_t value)
{
    char digits[5];
    uint32_t count = 0u;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value = (uint16_t)(value / 10u);
    } while (value != 0u);
    while (count != 0u) {
        if (*length + 1u >= capacity) {
            return 0;
        }
        buffer[(*length)++] = digits[--count];
    }
    buffer[*length] = '\0';
    return 1;
}

static void write_ethernet_header(uint8_t *frame, const uint8_t *destination,
                                  uint16_t protocol)
{
    copy_bytes(frame, destination, 6u);
    copy_bytes(frame + 6u, hardware_address, 6u);
    write_be16(frame + 12u, protocol);
}

static uint8_t *write_ipv4_header(uint8_t *frame, const uint8_t *destination,
                                  uint8_t protocol, uint16_t payload_length)
{
    uint8_t *ip = frame + ETHERNET_HEADER_SIZE;
    ip[0] = 0x45u;
    ip[1] = 0u;
    write_be16(ip + 2u, (uint16_t)(IPV4_HEADER_SIZE + payload_length));
    write_be16(ip + 4u, ++ipv4_identifier);
    write_be16(ip + 6u, 0x4000u);
    ip[8] = 64u;
    ip[9] = protocol;
    ip[10] = 0u;
    ip[11] = 0u;
    copy_bytes(ip + 12u, configuration.address, 4u);
    copy_bytes(ip + 16u, destination, 4u);
    write_be16(ip + 10u, internet_checksum(ip, IPV4_HEADER_SIZE));
    return ip;
}

static int send_udp(const uint8_t *ethernet_destination,
                    const uint8_t *ip_destination, uint16_t source_port,
                    uint16_t destination_port, const uint8_t *payload,
                    uint16_t payload_length)
{
    uint32_t frame_length = ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE +
                            UDP_HEADER_SIZE + payload_length;
    if (frame_length > sizeof(transmit_packet)) {
        return 0;
    }
    write_ethernet_header(transmit_packet, ethernet_destination,
                          ETHERNET_IPV4);
    uint8_t *ip = write_ipv4_header(
        transmit_packet, ip_destination, IPV4_UDP,
        (uint16_t)(UDP_HEADER_SIZE + payload_length));
    uint8_t *udp = ip + IPV4_HEADER_SIZE;
    write_be16(udp, source_port);
    write_be16(udp + 2u, destination_port);
    write_be16(udp + 4u, (uint16_t)(UDP_HEADER_SIZE + payload_length));
    write_be16(udp + 6u, 0u);
    copy_bytes(udp + UDP_HEADER_SIZE, payload, payload_length);
    if (frame_length < 60u) {
        for (uint32_t index = frame_length; index < 60u; ++index) {
            transmit_packet[index] = 0u;
        }
        frame_length = 60u;
    }
    return active_link->send(transmit_packet, (uint16_t)frame_length);
}

static uint16_t make_dhcp_message(uint8_t *payload, uint8_t message_type,
                                  const struct dhcp_reply *request)
{
    for (uint32_t index = 0u; index < DHCP_MAX_SIZE; ++index) {
        payload[index] = 0u;
    }
    payload[0] = 1u;
    payload[1] = 1u;
    payload[2] = 6u;
    write_be32(payload + 4u, DHCP_TRANSACTION_ID);
    if (message_type == 3u && request != 0) {
        copy_bytes(payload + 12u, request->address, 4u);
    }
    payload[10] = 0x80u;
    copy_bytes(payload + 28u, hardware_address, 6u);
    write_be32(payload + 236u, 0x63825363u);

    uint32_t option = DHCP_FIXED_SIZE;
    payload[option++] = 53u;
    payload[option++] = 1u;
    payload[option++] = message_type;
    if (message_type == 3u && request != 0) {
        payload[option++] = 50u;
        payload[option++] = 4u;
        copy_bytes(payload + option, request->address, 4u);
        option += 4u;
        payload[option++] = 54u;
        payload[option++] = 4u;
        copy_bytes(payload + option, request->server, 4u);
        option += 4u;
    }
    payload[option++] = 55u;
    payload[option++] = 4u;
    payload[option++] = 1u;
    payload[option++] = 3u;
    payload[option++] = 6u;
    payload[option++] = 51u;
    payload[option++] = 255u;
    return (uint16_t)option;
}

static int parse_ipv4_udp(const uint8_t *frame, uint16_t frame_length,
                          const uint8_t **ip_output,
                          const uint8_t **udp_output,
                          const uint8_t **payload_output,
                          uint16_t *payload_length)
{
    if (frame_length < ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE ||
        read_be16(frame + 12u) != ETHERNET_IPV4) {
        return 0;
    }
    const uint8_t *ip = frame + ETHERNET_HEADER_SIZE;
    uint32_t header_length = (uint32_t)(ip[0] & 0x0fu) * 4u;
    uint16_t total_length = read_be16(ip + 2u);
    if ((ip[0] >> 4) != 4u || header_length < IPV4_HEADER_SIZE ||
        total_length < header_length + UDP_HEADER_SIZE ||
        total_length > frame_length - ETHERNET_HEADER_SIZE ||
        ip[9] != IPV4_UDP) {
        return 0;
    }
    const uint8_t *udp = ip + header_length;
    uint16_t udp_length = read_be16(udp + 4u);
    if (udp_length < UDP_HEADER_SIZE ||
        udp_length > total_length - header_length) {
        return 0;
    }
    *ip_output = ip;
    *udp_output = udp;
    *payload_output = udp + UDP_HEADER_SIZE;
    *payload_length = (uint16_t)(udp_length - UDP_HEADER_SIZE);
    return 1;
}

static int parse_dhcp_reply(const uint8_t *frame, uint16_t frame_length,
                            struct dhcp_reply *reply)
{
    const uint8_t *ip;
    const uint8_t *udp;
    const uint8_t *payload;
    uint16_t payload_length;
    if (!parse_ipv4_udp(frame, frame_length, &ip, &udp, &payload,
                        &payload_length) ||
        read_be16(udp + 2u) != 68u || payload_length < DHCP_FIXED_SIZE ||
        payload[0] != 2u || payload[1] != 1u || payload[2] != 6u ||
        read_be32(payload + 4u) != DHCP_TRANSACTION_ID ||
        !bytes_equal(payload + 28u, hardware_address, 6u) ||
        read_be32(payload + 236u) != 0x63825363u) {
        return 0;
    }

    reply->message_type = 0u;
    copy_bytes(reply->address, payload + 16u, 4u);
    copy_bytes(reply->server, ip + 12u, 4u);
    uint32_t option = DHCP_FIXED_SIZE;
    while (option < payload_length) {
        uint8_t code = payload[option++];
        if (code == 255u) {
            break;
        }
        if (code == 0u) {
            continue;
        }
        if (option >= payload_length) {
            return 0;
        }
        uint8_t length = payload[option++];
        if (option + length > payload_length) {
            return 0;
        }
        if (code == 53u && length == 1u) {
            reply->message_type = payload[option];
        } else if (code == 1u && length == 4u) {
            copy_bytes(reply->subnet_mask, payload + option, 4u);
        } else if (code == 3u && length >= 4u) {
            copy_bytes(reply->gateway, payload + option, 4u);
        } else if (code == 6u && length >= 4u) {
            copy_bytes(reply->dns_server, payload + option, 4u);
        } else if (code == 54u && length == 4u) {
            copy_bytes(reply->server, payload + option, 4u);
        }
        option += length;
    }
    return reply->message_type != 0u;
}

static int wait_for_dhcp(uint8_t message_type, struct dhcp_reply *reply)
{
    uint32_t start = pit_ticks_read();
    while ((uint32_t)(pit_ticks_read() - start) < DHCP_TIMEOUT_TICKS) {
        uint16_t frame_length;
        if (active_link->receive(receive_packet, sizeof(receive_packet),
                                 &frame_length) &&
            parse_dhcp_reply(receive_packet, frame_length, reply)) {
            if (reply->message_type == message_type) {
                return 1;
            }
            if (reply->message_type == 6u) {
                return 0;
            }
        }
        __asm__ volatile ("hlt");
    }
    return 0;
}

static int send_arp_request(const uint8_t target[4])
{
    static const uint8_t broadcast[6] = {
        0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
    };
    write_ethernet_header(transmit_packet, broadcast, ETHERNET_ARP);
    uint8_t *arp = transmit_packet + ETHERNET_HEADER_SIZE;
    write_be16(arp, 1u);
    write_be16(arp + 2u, ETHERNET_IPV4);
    arp[4] = 6u;
    arp[5] = 4u;
    write_be16(arp + 6u, 1u);
    copy_bytes(arp + 8u, hardware_address, 6u);
    copy_bytes(arp + 14u, configuration.address, 4u);
    for (uint32_t index = 18u; index < 24u; ++index) {
        arp[index] = 0u;
    }
    copy_bytes(arp + 24u, target, 4u);
    for (uint32_t index = 42u; index < 60u; ++index) {
        transmit_packet[index] = 0u;
    }
    return active_link->send(transmit_packet, 60u);
}

static int wait_for_arp(const uint8_t target[4], uint8_t mac[6])
{
    uint32_t start = pit_ticks_read();
    while ((uint32_t)(pit_ticks_read() - start) < PING_TIMEOUT_TICKS) {
        uint16_t frame_length;
        if (active_link->receive(receive_packet, sizeof(receive_packet),
                                 &frame_length) &&
            frame_length >= ETHERNET_HEADER_SIZE + 28u &&
            read_be16(receive_packet + 12u) == ETHERNET_ARP) {
            const uint8_t *arp = receive_packet + ETHERNET_HEADER_SIZE;
            if (read_be16(arp) == 1u &&
                read_be16(arp + 2u) == ETHERNET_IPV4 &&
                read_be16(arp + 6u) == 2u &&
                bytes_equal(arp + 14u, target, 4u) &&
                bytes_equal(arp + 24u, configuration.address, 4u)) {
                copy_bytes(mac, arp + 8u, 6u);
                return 1;
            }
        }
        __asm__ volatile ("hlt");
    }
    return 0;
}

static int resolve_ipv4_mac(const uint8_t address[4], uint8_t mac[6])
{
    uint8_t next_hop[4];
    int same_subnet = 1;
    for (uint32_t index = 0u; index < 4u; ++index) {
        if ((address[index] & configuration.subnet_mask[index]) !=
            (configuration.address[index] & configuration.subnet_mask[index])) {
            same_subnet = 0;
        }
    }
    copy_bytes(next_hop, same_subnet ? address : configuration.gateway, 4u);
    return send_arp_request(next_hop) && wait_for_arp(next_hop, mac);
}

static int send_tcp_segment(const uint8_t destination_mac[6],
                            const uint8_t destination_ip[4],
                            uint16_t source_port, uint16_t destination_port,
                            uint32_t sequence, uint32_t acknowledgment,
                            uint8_t flags, const uint8_t *payload,
                            uint16_t payload_length)
{
    uint16_t tcp_length = (uint16_t)(20u + payload_length);
    uint32_t frame_length = ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE +
                            tcp_length;
    if (frame_length > sizeof(transmit_packet) ||
        (payload_length != 0u && payload == 0)) {
        return 0;
    }
    write_ethernet_header(transmit_packet, destination_mac, ETHERNET_IPV4);
    uint8_t *ip = write_ipv4_header(transmit_packet, destination_ip, 6u,
                                    tcp_length);
    uint8_t *tcp = ip + IPV4_HEADER_SIZE;
    write_be16(tcp, source_port);
    write_be16(tcp + 2u, destination_port);
    write_be32(tcp + 4u, sequence);
    write_be32(tcp + 8u, acknowledgment);
    tcp[12] = 0x50u;
    tcp[13] = flags;
    write_be16(tcp + 14u, 4096u);
    tcp[16] = 0u;
    tcp[17] = 0u;
    tcp[18] = 0u;
    tcp[19] = 0u;
    if (payload_length != 0u) {
        copy_bytes(tcp + 20u, payload, payload_length);
    }
    write_be16(tcp + 16u, tcp_checksum(tcp, tcp_length, destination_ip));
    if (frame_length < 60u) {
        for (uint32_t index = frame_length; index < 60u; ++index) {
            transmit_packet[index] = 0u;
        }
        frame_length = 60u;
    }
    return active_link->send(transmit_packet, (uint16_t)frame_length);
}

static int parse_tcp_segment(const uint8_t *frame, uint16_t frame_length,
                             const uint8_t source_ip[4],
                             uint16_t source_port, uint16_t local_port,
                             struct tcp_segment *segment)
{
    if (frame_length < ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE + 20u ||
        read_be16(frame + 12u) != ETHERNET_IPV4) {
        return 0;
    }
    const uint8_t *ip = frame + ETHERNET_HEADER_SIZE;
    uint32_t ip_header_length = (uint32_t)(ip[0] & 0x0fu) * 4u;
    uint16_t ip_length = read_be16(ip + 2u);
    if ((ip[0] >> 4) != 4u || ip_header_length < IPV4_HEADER_SIZE ||
        ip_length < ip_header_length + 20u ||
        ip_length > frame_length - ETHERNET_HEADER_SIZE ||
        ip[9] != 6u ||
        !bytes_equal(ip + 12u, source_ip, 4u) ||
        !bytes_equal(ip + 16u, configuration.address, 4u)) {
        return 0;
    }
    const uint8_t *tcp = ip + ip_header_length;
    uint32_t tcp_header_length = (uint32_t)(tcp[12] >> 4) * 4u;
    if (tcp_header_length < 20u ||
        tcp_header_length > ip_length - ip_header_length ||
        read_be16(tcp) != source_port ||
        read_be16(tcp + 2u) != local_port) {
        return 0;
    }
    segment->sequence = read_be32(tcp + 4u);
    segment->acknowledgment = read_be32(tcp + 8u);
    segment->flags = tcp[13];
    segment->payload = tcp + tcp_header_length;
    segment->payload_length =
        (uint16_t)(ip_length - ip_header_length - tcp_header_length);
    return 1;
}

static int send_icmp_echo(const uint8_t destination_mac[6],
                          const uint8_t destination_ip[4],
                          uint16_t sequence)
{
    const uint16_t icmp_length = 16u;
    write_ethernet_header(transmit_packet, destination_mac, ETHERNET_IPV4);
    uint8_t *ip = write_ipv4_header(transmit_packet, destination_ip,
                                    IPV4_ICMP, icmp_length);
    uint8_t *icmp = ip + IPV4_HEADER_SIZE;
    icmp[0] = 8u;
    icmp[1] = 0u;
    icmp[2] = 0u;
    icmp[3] = 0u;
    write_be16(icmp + 4u, ICMP_IDENTIFIER);
    write_be16(icmp + 6u, sequence);
    write_be32(icmp + 8u, DHCP_TRANSACTION_ID);
    write_be32(icmp + 12u, pit_ticks_read());
    write_be16(icmp + 2u, internet_checksum(icmp, icmp_length));
    return active_link->send(transmit_packet,
                             ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE +
                             icmp_length);
}

static int is_icmp_echo_reply(const uint8_t *frame, uint16_t frame_length,
                              const uint8_t source_ip[4], uint16_t sequence)
{
    if (frame_length < ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE + 8u ||
        read_be16(frame + 12u) != ETHERNET_IPV4) {
        return 0;
    }
    const uint8_t *ip = frame + ETHERNET_HEADER_SIZE;
    uint32_t header_length = (uint32_t)(ip[0] & 0x0fu) * 4u;
    uint16_t total_length = read_be16(ip + 2u);
    if ((ip[0] >> 4) != 4u || header_length < IPV4_HEADER_SIZE ||
        total_length < header_length + 8u ||
        total_length > frame_length - ETHERNET_HEADER_SIZE ||
        ip[9] != IPV4_ICMP ||
        !bytes_equal(ip + 12u, source_ip, 4u) ||
        !bytes_equal(ip + 16u, configuration.address, 4u)) {
        return 0;
    }
    const uint8_t *icmp = ip + header_length;
    return icmp[0] == 0u && icmp[1] == 0u &&
           read_be16(icmp + 4u) == ICMP_IDENTIFIER &&
           read_be16(icmp + 6u) == sequence;
}

int network_initialize(void)
{
    active_link = network_link_initialize();
    network_available = (uint8_t)(active_link != 0);
    configuration.configured = 0u;
    ipv4_identifier = 0u;
    if (network_available != 0u) {
        active_link->get_mac(hardware_address);
    }
    return network_available != 0u;
}

int network_is_available(void)
{
    return network_available != 0u;
}

const char *network_interface_name(void)
{
    return active_link == 0 ? "NONE" : active_link->name;
}

int network_dhcp(void)
{
    if (network_available == 0u) {
        dhcp_error = "no supported network interface";
        return 0;
    }
    static const uint8_t broadcast_ip[4] = {
        255u, 255u, 255u, 255u,
    };
    static const uint8_t broadcast_mac[6] = {
        0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
    };
    struct dhcp_reply offer = {0};
    struct dhcp_reply acknowledgment = {0};

    configuration.configured = 0u;
    for (uint32_t index = 0u; index < 4u; ++index) {
        configuration.address[index] = 0u;
        configuration.subnet_mask[index] = 0u;
        configuration.gateway[index] = 0u;
        configuration.dns_server[index] = 0u;
    }
    uint16_t payload_length = make_dhcp_message(dhcp_payload, 1u, 0);
    if (!send_udp(broadcast_mac, broadcast_ip, 68u, 67u,
                  dhcp_payload, payload_length)) {
        dhcp_error = "DHCP discovery transmit failed";
        return 0;
    }
    if (!wait_for_dhcp(2u, &offer) || offer.address[0] == 0u) {
        dhcp_error = "no valid DHCP offer received";
        return 0;
    }

    payload_length = make_dhcp_message(dhcp_payload, 3u, &offer);
    if (!send_udp(broadcast_mac, broadcast_ip, 68u, 67u,
                  dhcp_payload, payload_length)) {
        dhcp_error = "DHCP request transmit failed";
        return 0;
    }
    if (!wait_for_dhcp(5u, &acknowledgment) ||
        acknowledgment.address[0] == 0u) {
        dhcp_error = "no valid DHCP acknowledgment received";
        return 0;
    }
    copy_bytes(configuration.address, acknowledgment.address, 4u);
    copy_bytes(configuration.subnet_mask, acknowledgment.subnet_mask, 4u);
    copy_bytes(configuration.gateway, acknowledgment.gateway, 4u);
    copy_bytes(configuration.dns_server, acknowledgment.dns_server, 4u);
    configuration.configured = 1u;
    dhcp_error = 0;
    return 1;
}

const char *network_dhcp_error(void)
{
    return dhcp_error == 0 ? "unspecified DHCP error" : dhcp_error;
}

static int dns_skip_name(const uint8_t *message, uint16_t length,
                         uint32_t *offset)
{
    uint32_t cursor = *offset;
    uint32_t resume = 0u;
    uint8_t jumped = 0u;
    uint32_t steps = 0u;
    while (cursor < length && ++steps <= length) {
        uint8_t label = message[cursor++];
        if (label == 0u) {
            *offset = jumped != 0u ? resume : cursor;
            return 1;
        }
        if ((label & 0xc0u) == 0xc0u) {
            if (cursor >= length) {
                return 0;
            }
            uint32_t target =
                ((uint32_t)(label & 0x3fu) << 8) | message[cursor++];
            if (target >= length) {
                return 0;
            }
            if (jumped == 0u) {
                resume = cursor;
                jumped = 1u;
            }
            cursor = target;
            continue;
        }
        if ((label & 0xc0u) != 0u || cursor + label > length) {
            return 0;
        }
        cursor += label;
    }
    return 0;
}

static int parse_dns_response(const uint8_t *message, uint16_t length,
                              uint16_t identifier, uint8_t address[4])
{
    if (length < 12u || read_be16(message) != identifier) {
        return 0;
    }
    uint16_t flags = read_be16(message + 2u);
    uint16_t questions = read_be16(message + 4u);
    uint16_t answers = read_be16(message + 6u);
    if ((flags & 0x8000u) == 0u || (flags & 0x7800u) != 0u ||
        (flags & 0x000fu) != 0u || answers == 0u || questions == 0u) {
        return 0;
    }

    uint32_t offset = 12u;
    for (uint32_t index = 0u; index < questions; ++index) {
        if (!dns_skip_name(message, length, &offset) ||
            offset + 4u > length) {
            return 0;
        }
        offset += 4u;
    }
    for (uint32_t index = 0u; index < answers; ++index) {
        if (!dns_skip_name(message, length, &offset) ||
            offset + 10u > length) {
            return 0;
        }
        uint16_t type = read_be16(message + offset);
        uint16_t record_class = read_be16(message + offset + 2u);
        uint16_t record_length = read_be16(message + offset + 8u);
        offset += 10u;
        if (offset + record_length > length) {
            return 0;
        }
        if (type == 1u && record_class == 1u && record_length == 4u) {
            copy_bytes(address, message + offset, 4u);
            return 1;
        }
        offset += record_length;
    }
    return 0;
}

int network_resolve_ipv4(const char *hostname, uint8_t address[4])
{
    if (network_available == 0u || configuration.configured == 0u ||
        hostname == 0 || address == 0 ||
        (configuration.dns_server[0] | configuration.dns_server[1] |
         configuration.dns_server[2] | configuration.dns_server[3]) == 0u) {
        return 0;
    }

    uint16_t query_length = 12u;
    uint32_t label_length = 0u;
    uint32_t name_length = 0u;
    uint32_t label_position = query_length++;
    for (uint32_t index = 0u; ; ++index) {
        char character = hostname[index];
        if (character == '.' || character == '\0') {
            if (label_length == 0u || label_length > 63u) {
                return 0;
            }
            dns_payload[label_position] = (uint8_t)label_length;
            name_length += label_length + 1u;
            label_length = 0u;
            if (character == '\0' ||
                (character == '.' && hostname[index + 1u] == '\0')) {
                if (name_length + 1u > 255u ||
                    query_length + 5u > sizeof(dns_payload)) {
                    return 0;
                }
                dns_payload[query_length++] = 0u;
                break;
            }
            label_position = query_length++;
            continue;
        }
        if (character < '!' || character > '~' || label_length >= 63u ||
            name_length + label_length + 3u > 255u ||
            query_length + 6u > sizeof(dns_payload)) {
            return 0;
        }
        dns_payload[query_length++] = (uint8_t)character;
        ++label_length;
    }

    uint16_t identifier = ++dns_identifier;
    write_be16(dns_payload, identifier);
    write_be16(dns_payload + 2u, 0x0100u);
    write_be16(dns_payload + 4u, 1u);
    write_be16(dns_payload + 6u, 0u);
    write_be16(dns_payload + 8u, 0u);
    write_be16(dns_payload + 10u, 0u);
    write_be16(dns_payload + query_length, 1u);
    write_be16(dns_payload + query_length + 2u, 1u);
    query_length = (uint16_t)(query_length + 4u);

    uint8_t destination_mac[6];
    if (!resolve_ipv4_mac(configuration.dns_server, destination_mac)) {
        return 0;
    }
    uint16_t local_port = (uint16_t)(53000u + identifier % 2000u);
    if (!send_udp(destination_mac, configuration.dns_server, local_port, 53u,
                  dns_payload, query_length)) {
        return 0;
    }

    uint32_t start = pit_ticks_read();
    while ((uint32_t)(pit_ticks_read() - start) < DNS_TIMEOUT_TICKS) {
        uint16_t frame_length;
        const uint8_t *ip;
        const uint8_t *udp;
        const uint8_t *payload;
        uint16_t payload_length;
        if (active_link->receive(receive_packet, sizeof(receive_packet),
                                 &frame_length) &&
            parse_ipv4_udp(receive_packet, frame_length, &ip, &udp, &payload,
                           &payload_length) &&
            read_be16(udp) == 53u && read_be16(udp + 2u) == local_port &&
            bytes_equal(ip + 12u, configuration.dns_server, 4u) &&
            bytes_equal(ip + 16u, configuration.address, 4u) &&
            parse_dns_response(payload, payload_length, identifier, address)) {
            return 1;
        }
        __asm__ volatile ("hlt");
    }
    return 0;
}

int network_ping(const uint8_t address[4], uint32_t *round_trip_ms)
{
    if (network_available == 0u || configuration.configured == 0u ||
        address == 0) {
        return 0;
    }

    uint8_t destination_mac[6];
    if (!resolve_ipv4_mac(address, destination_mac) ||
        !send_icmp_echo(destination_mac, address, 1u)) {
        return 0;
    }

    uint32_t start = pit_ticks_read();
    while ((uint32_t)(pit_ticks_read() - start) < PING_TIMEOUT_TICKS) {
        uint16_t frame_length;
        if (active_link->receive(receive_packet, sizeof(receive_packet),
                                 &frame_length) &&
            is_icmp_echo_reply(receive_packet, frame_length, address, 1u)) {
            if (round_trip_ms != 0) {
                *round_trip_ms =
                    (uint32_t)(pit_ticks_read() - start) * 10u;
            }
            return 1;
        }
        __asm__ volatile ("hlt");
    }
    return 0;
}

int network_http_get(const uint8_t address[4], uint16_t port,
                     const char *host, const char *path, char *response,
                     uint32_t capacity, uint32_t *response_length)
{
    if (network_available == 0u || configuration.configured == 0u ||
        address == 0 || host == 0 || path == 0 || response == 0 ||
        capacity < 2u ||
        response_length == 0 || *path != '/') {
        return 0;
    }
    uint32_t host_length = 0u;
    while (host[host_length] != '\0') {
        char character = host[host_length];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '.' || character == '-')) {
            return 0;
        }
        if (++host_length >= 64u) {
            return 0;
        }
    }
    if (host_length == 0u) {
        return 0;
    }
    for (uint32_t index = 0u; path[index] != '\0'; ++index) {
        if (path[index] < ' ' || path[index] == '\r' || path[index] == '\n') {
            return 0;
        }
    }

    uint8_t destination_mac[6];
    if (!resolve_ipv4_mac(address, destination_mac)) {
        return 0;
    }

    char request[192];
    uint32_t request_length = 0u;
    if (!append_text(request, sizeof(request), &request_length, "GET ") ||
        !append_text(request, sizeof(request), &request_length, path) ||
        !append_text(request, sizeof(request), &request_length,
                     " HTTP/1.0\r\nHost: ")) {
        return 0;
    }
    if (!append_text(request, sizeof(request), &request_length, host)) {
        return 0;
    }
    if (port != 80u &&
        (!append_text(request, sizeof(request), &request_length, ":") ||
         !append_decimal(request, sizeof(request), &request_length, port))) {
        return 0;
    }
    if (!append_text(request, sizeof(request), &request_length,
                     "\r\nConnection: close\r\n\r\n")) {
        return 0;
    }

    uint16_t local_port = (uint16_t)(49152u + pit_ticks_read() % 16384u);
    uint32_t local_sequence = pit_ticks_read() ^ DHCP_TRANSACTION_ID;
    if (!send_tcp_segment(destination_mac, address, local_port, port,
                          local_sequence, 0u, 0x02u, 0, 0u)) {
        return 0;
    }

    uint32_t remote_sequence = 0u;
    uint32_t start = pit_ticks_read();
    int connected = 0;
    while ((uint32_t)(pit_ticks_read() - start) < TCP_TIMEOUT_TICKS) {
        uint16_t frame_length;
        struct tcp_segment segment;
        if (active_link->receive(receive_packet, sizeof(receive_packet),
                                 &frame_length) &&
            parse_tcp_segment(receive_packet, frame_length, address, port,
                              local_port, &segment)) {
            if ((segment.flags & 0x04u) != 0u) {
                return 0;
            }
            if ((segment.flags & 0x12u) == 0x12u &&
                segment.acknowledgment == local_sequence + 1u) {
                remote_sequence = segment.sequence + 1u;
                ++local_sequence;
                connected = send_tcp_segment(
                    destination_mac, address, local_port, port,
                    local_sequence, remote_sequence, 0x10u, 0, 0u);
                break;
            }
        }
        __asm__ volatile ("hlt");
    }
    if (!connected) {
        return 0;
    }

    if (!send_tcp_segment(destination_mac, address, local_port, port,
                          local_sequence, remote_sequence, 0x18u,
                          (const uint8_t *)request,
                          (uint16_t)request_length)) {
        return 0;
    }
    local_sequence += request_length;

    uint32_t stored_length = 0u;
    start = pit_ticks_read();
    while ((uint32_t)(pit_ticks_read() - start) < TCP_TIMEOUT_TICKS) {
        uint16_t frame_length;
        struct tcp_segment segment;
        if (active_link->receive(receive_packet, sizeof(receive_packet),
                                 &frame_length) &&
            parse_tcp_segment(receive_packet, frame_length, address, port,
                              local_port, &segment)) {
            if ((segment.flags & 0x04u) != 0u) {
                break;
            }
            if (segment.payload_length != 0u &&
                segment.sequence == remote_sequence) {
                for (uint32_t index = 0u; index < segment.payload_length;
                     ++index) {
                    if (stored_length + 1u < capacity) {
                        response[stored_length++] =
                            (char)segment.payload[index];
                    }
                }
                remote_sequence += segment.payload_length;
            }
            if ((segment.flags & 0x01u) != 0u &&
                segment.sequence + segment.payload_length == remote_sequence) {
                ++remote_sequence;
                (void)send_tcp_segment(destination_mac, address, local_port,
                                       port, local_sequence, remote_sequence,
                                       0x10u, 0, 0u);
                response[stored_length] = '\0';
                *response_length = stored_length;
                return stored_length != 0u;
            }
            if (segment.payload_length != 0u) {
                (void)send_tcp_segment(destination_mac, address, local_port,
                                       port, local_sequence, remote_sequence,
                                       0x10u, 0, 0u);
            }
        }
        __asm__ volatile ("hlt");
    }
    response[stored_length] = '\0';
    *response_length = stored_length;
    return 0;
}

void network_get_config(struct network_config *config)
{
    if (config == 0) {
        return;
    }
    *config = configuration;
}

void network_get_mac(uint8_t address[6])
{
    if (address != 0 && active_link != 0) {
        copy_bytes(address, hardware_address, 6u);
    }
}
