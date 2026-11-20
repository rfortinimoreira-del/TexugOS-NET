#include "link.h"
#include "rtl8139.h"
#include "rtl8192se.h"

static const struct network_link rtl8192se_link = {
    .name = "RTL8191SE Wi-Fi",
    .send = rtl8192se_network_send,
    .receive = rtl8192se_network_receive,
    .get_mac = rtl8192se_network_get_mac,
};

static const struct network_link rtl8139_link = {
    .name = "RTL8139",
    .send = rtl8139_send,
    .receive = rtl8139_receive,
    .get_mac = rtl8139_get_mac,
};

const struct network_link *network_link_initialize(void)
{
    if (rtl8192se_network_is_associated()) {
        return &rtl8192se_link;
    }
    return rtl8139_initialize() ? &rtl8139_link : 0;
}
