#include <common.h>
#include <os.h>
#include <lwip/netif.h>

#include <usb/gadget_ecm.h>

#include <lwip/apps/dhcp_server.h>

static const struct gadget_id usb_id = {
    .vendor_id     = 0x0525,
    .product_id    = 0xa4a1,
};

struct usb_ecm_param ecm_param = {
    .ipaddr = "192.188.1.1",
    .netmask = "255.255.255.0",
    .gw = "192.188.1.1",
};

static void dhcp_event(const struct dhcp_server_event *event, void *user)
{
    const char *type = "unknown";

    if (!event)
        return;

    switch (event->type) {
    case DHCP_SERVER_EVENT_OFFER:
        type = "offer";
        break;
    case DHCP_SERVER_EVENT_ACK:
        type = "ack";
        break;
    case DHCP_SERVER_EVENT_RELEASE:
        type = "release";
        break;
    default:
        break;
    }

    printf("[dhcp] %s %02x:%02x:%02x:%02x:%02x:%02x -> %u.%u.%u.%u\n",
           type,
           event->mac[0], event->mac[1], event->mac[2],
           event->mac[3], event->mac[4], event->mac[5],
           ip4_addr1(&event->ip), ip4_addr2(&event->ip),
           ip4_addr3(&event->ip), ip4_addr4(&event->ip));

    (void)user;
}

static struct dhcp_server_config dhcp_config = {
    .lease_start = "192.188.1.2",
    .lease_count = 8,
    .dns = "",
    .gateway = "",
    .lease_time_seconds = 86400,
    .event_cb = dhcp_event,
    .event_cb_user = NULL,
};

static struct dhcp_server *g_dhcp_server;

static void start_dhcp_server(struct netif *netif)
{
    if (!netif || g_dhcp_server)
        return;

    g_dhcp_server = lwip_dhcp_server_start(netif, &dhcp_config);
    if (g_dhcp_server)
        printf("[dhcp] server started on %c%c\n", netif->name[0], netif->name[1]);
    else
        printf("[dhcp] failed to start server\n");
}

__attribute__((__unused__)) static void stop_dhcp_server(void)
{
    if (!g_dhcp_server)
        return;

    lwip_dhcp_server_stop(g_dhcp_server);
    g_dhcp_server = NULL;
}

int gadget_usb_ecm_dhcp_test(void)
{
    int ret = gadget_ecm_init(&usb_id, &ecm_param);
    if (ret < 0)
        return ret;

    struct netif *netif = gadget_ecm_get_netif();
    if (netif) {
        start_dhcp_server(netif);
        return 0;
    } else {
        printf("[dhcp] failed to get netif\n");
        return -1;
    }
}