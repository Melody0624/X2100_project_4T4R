#ifndef DHCP_SERVER_H
#define DHCP_SERVER_H

#include <stdint.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>

struct dhcp_server;

enum dhcp_server_event_type {
    DHCP_SERVER_EVENT_OFFER,
    DHCP_SERVER_EVENT_ACK,
    DHCP_SERVER_EVENT_RELEASE,
    DHCP_SERVER_EVENT_DECLINE,
    DHCP_SERVER_EVENT_INFORM,
};

struct dhcp_server_event {
    enum dhcp_server_event_type type;
    ip4_addr_t ip;
    uint8_t mac[6];
};

typedef void (*dhcp_server_event_cb)(const struct dhcp_server_event *event,
                                     void *user);

struct dhcp_server_config {
    const char *lease_start;          /* First address in the pool */
    uint32_t    lease_count;          /* Number of sequential leases */
    const char *dns;                  /* Optional DNS override, "" or "0.0.0.0" disables DNS */
    const char *gateway;              /* Optional gateway override, "" or "0.0.0.0" disables router option */
    uint32_t    lease_time_seconds;   /* Lease duration in seconds */
    dhcp_server_event_cb event_cb;    /* Optional notifications */
    void *event_cb_user;              /* Callback context */
};

struct dhcp_server *lwip_dhcp_server_start(struct netif *netif,
                                      const struct dhcp_server_config *config);
void lwip_dhcp_server_stop(struct dhcp_server *srv);
const ip4_addr_t *lwip_dhcp_server_current_lease(const struct dhcp_server *srv);

#endif /* DHCP_SERVER_H */
