#ifndef TEXUGOS_NETWORK_H
#define TEXUGOS_NETWORK_H

#include <stdint.h>

struct network_config {
    uint8_t address[4];
    uint8_t subnet_mask[4];
    uint8_t gateway[4];
    uint8_t dns_server[4];
    uint8_t configured;
};

int network_initialize(void);
int network_is_available(void);
const char *network_interface_name(void);
int network_dhcp(void);
const char *network_dhcp_error(void);
int network_resolve_ipv4(const char *hostname, uint8_t address[4]);
int network_ping(const uint8_t address[4], uint32_t *round_trip_ms);
int network_http_get(const uint8_t address[4], uint16_t port,
                     const char *host, const char *path, char *response,
                     uint32_t capacity, uint32_t *response_length);
void network_get_config(struct network_config *config);
void network_get_mac(uint8_t address[6]);

#endif
