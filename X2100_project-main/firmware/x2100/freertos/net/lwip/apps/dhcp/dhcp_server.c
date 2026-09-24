#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <lwip/opt.h>
#include <lwip/udp.h>
#include <lwip/pbuf.h>
#include <lwip/ip_addr.h>
#include <lwip/ip4_addr.h>
#include <lwip/inet.h>
#include <lwip/netif.h>
#include <lwip/prot/dhcp.h>
#include <lwip/def.h>
#include <lwip/tcpip.h>
#include <lwip/sys.h>

#include <lwip/apps/dhcp_server.h>

#ifndef DHCP_HTYPE_ETH
#define DHCP_HTYPE_ETH 1
#endif

#ifndef DHCP_HLEN_ETH
#define DHCP_HLEN_ETH 6
#endif

#ifndef DHCP_BROADCAST_FLAG
#define DHCP_BROADCAST_FLAG 0x8000
#endif

#define DHCP_SERVER_PORT          67
#define DHCP_CLIENT_PORT          68
#define DHCP_FIXED_LEN            (sizeof(struct dhcp_msg) - DHCP_OPTIONS_LEN)
#define DHCP_DEFAULT_LEASE_TIME_S 86400U

extern struct udp_pcb *udp_pcbs;

struct dhcp_server_lease {
    ip4_addr_t ip;
    u8_t mac[6];
    u8_t mac_set;
    u8_t active;
    u32_t expiry_ms;
};

struct dhcp_server {
    struct udp_pcb *pcb;
    struct netif *netif;
    ip4_addr_t server_ip;
    ip4_addr_t netmask;
    ip4_addr_t gateway;
    ip4_addr_t dns;
    ip4_addr_t pool_start;
    u32_t pool_count;
    u32_t lease_time_s;
    struct dhcp_server_lease *leases;
    dhcp_server_event_cb event_cb;
    void *event_cb_user;
};

static void dhcp_server_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                             const ip_addr_t *addr, u16_t port);

static const u8_t *dhcp_get_option(const u8_t *options, u16_t options_len,
                                   u8_t option, u8_t *option_len)
{
    u16_t idx = 0;

    while (idx < options_len) {
        u8_t code = options[idx++];

        if (code == DHCP_OPTION_END)
            break;
        if (code == DHCP_OPTION_PAD)
            continue;
        if (idx >= options_len)
            break;

        u8_t len = options[idx++];
        if ((u16_t)(idx + len) > options_len)
            break;

        if (code == option) {
            if (option_len)
                *option_len = len;
            return &options[idx];
        }

        idx = (u16_t)(idx + len);
    }

    if (option_len)
        *option_len = 0;
    return NULL;
}

static u8_t *dhcp_put_option_byte(u8_t *opt, u8_t code, u8_t value)
{
    *opt++ = code;
    *opt++ = 1;
    *opt++ = value;
    return opt;
}

static u8_t *dhcp_put_option_u32(u8_t *opt, u8_t code, u32_t value)
{
    *opt++ = code;
    *opt++ = 4;
    *opt++ = (u8_t)(value >> 24);
    *opt++ = (u8_t)(value >> 16);
    *opt++ = (u8_t)(value >> 8);
    *opt++ = (u8_t)value;
    return opt;
}

static u8_t *dhcp_put_option_ip(u8_t *opt, u8_t code, const ip4_addr_t *addr)
{
    *opt++ = code;
    *opt++ = 4;
    *opt++ = ip4_addr1(addr);
    *opt++ = ip4_addr2(addr);
    *opt++ = ip4_addr3(addr);
    *opt++ = ip4_addr4(addr);
    return opt;
}

static int dhcp_server_ip_in_pool(const struct dhcp_server *srv,
                                  const ip4_addr_t *ip)
{
    u32_t host = lwip_ntohl(ip->addr);
    u32_t base = lwip_ntohl(srv->pool_start.addr);
    return (host >= base) && (host < base + srv->pool_count);
}

static struct dhcp_server_lease *dhcp_server_find_by_ip(struct dhcp_server *srv,
                                                        const ip4_addr_t *ip)
{
    u32_t host = lwip_ntohl(ip->addr);
    u32_t base = lwip_ntohl(srv->pool_start.addr);
    if (host < base)
        return NULL;

    u32_t idx = host - base;
    if (idx >= srv->pool_count)
        return NULL;

    return &srv->leases[idx];
}

static struct dhcp_server_lease *dhcp_server_find_by_mac(struct dhcp_server *srv,
                                                         const u8_t *mac,
                                                         int include_inactive)
{
    u32_t i;

    for (i = 0; i < srv->pool_count; ++i) {
        struct dhcp_server_lease *lease = &srv->leases[i];
        if (!lease->mac_set)
            continue;
        if (!include_inactive && !lease->active)
            continue;
        if (memcmp(lease->mac, mac, sizeof(lease->mac)) == 0)
            return lease;
    }

    return NULL;
}

static int dhcp_server_lease_expired(const struct dhcp_server_lease *lease)
{
    if (!lease->active)
        return 0;
    if (lease->expiry_ms == 0)
        return 0;
    u32_t now = sys_now();
    return (s32_t)(lease->expiry_ms - now) <= 0;
}

static void dhcp_server_cleanup_expired(struct dhcp_server *srv)
{
    u32_t i;

    for (i = 0; i < srv->pool_count; ++i) {
        struct dhcp_server_lease *lease = &srv->leases[i];
        if (dhcp_server_lease_expired(lease)) {
            lease->active = 0;
            lease->expiry_ms = 0;
        }
    }
}

static struct dhcp_server_lease *dhcp_server_first_free(struct dhcp_server *srv)
{
    u32_t i;

    for (i = 0; i < srv->pool_count; ++i) {
        struct dhcp_server_lease *lease = &srv->leases[i];
        if (dhcp_server_lease_expired(lease)) {
            lease->active = 0;
            lease->expiry_ms = 0;
        }
        if (!lease->active)
            return lease;
    }

    printf("[dhcp] no free lease (pool_count=%u)\n", srv->pool_count);
    return NULL;
}

static void dhcp_server_set_expiry(struct dhcp_server *srv,
                                   struct dhcp_server_lease *lease)
{
    if (!srv->lease_time_s) {
        lease->expiry_ms = 0;
        return;
    }

    if (srv->lease_time_s >= (UINT32_MAX / 1000U)) {
        lease->expiry_ms = 0;
        return;
    }

    u32_t lease_ms = srv->lease_time_s * 1000U;
    lease->expiry_ms = sys_now() + lease_ms;
}

static void dhcp_server_log_mac_ip(const char *tag,
                                   const u8_t *mac,
                                   const ip4_addr_t *ip)
{
    if (!mac)
        return;

    if (ip && ip->addr) {
        printf("[dhcp] %s %02x:%02x:%02x:%02x:%02x:%02x -> %u.%u.%u.%u\n",
               tag,
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
               ip4_addr1(ip), ip4_addr2(ip), ip4_addr3(ip), ip4_addr4(ip));
    } else {
        printf("[dhcp] %s %02x:%02x:%02x:%02x:%02x:%02x\n",
               tag,
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

static void dhcp_server_emit_event(struct dhcp_server *srv,
                                   enum dhcp_server_event_type type,
                                   const ip4_addr_t *ip,
                                   const u8_t *mac)
{
    if (!srv->event_cb || !ip || !mac)
        return;

    struct dhcp_server_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    ip4_addr_copy(event.ip, *ip);
    memcpy(event.mac, mac, sizeof(event.mac));
    srv->event_cb(&event, srv->event_cb_user);
}

static struct dhcp_server_lease *dhcp_server_prepare_offer(struct dhcp_server *srv,
                                                           const u8_t *mac,
                                                           const ip4_addr_t *requested)
{
    struct dhcp_server_lease *lease;

    lease = dhcp_server_find_by_mac(srv, mac, 1);
    if (lease) {
        printf("[dhcp] reuse lease for %02x:%02x:%02x:%02x:%02x:%02x -> %u.%u.%u.%u (active=%d)\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
               ip4_addr1(&lease->ip), ip4_addr2(&lease->ip),
               ip4_addr3(&lease->ip), ip4_addr4(&lease->ip),
               lease->active);
        return lease;
    }

    if (requested && requested->addr && dhcp_server_ip_in_pool(srv, requested)) {
        lease = dhcp_server_find_by_ip(srv, requested);
        if (lease && (!lease->active || (lease->mac_set && memcmp(lease->mac, mac, 6) == 0))) {
            printf("[dhcp] honour requested ip %u.%u.%u.%u\n",
                   ip4_addr1(&lease->ip), ip4_addr2(&lease->ip),
                   ip4_addr3(&lease->ip), ip4_addr4(&lease->ip));
            return lease;
        }
    }

    lease = dhcp_server_first_free(srv);
    if (lease)
        printf("[dhcp] allocate new lease %u.%u.%u.%u\n",
               ip4_addr1(&lease->ip), ip4_addr2(&lease->ip),
               ip4_addr3(&lease->ip), ip4_addr4(&lease->ip));
    return lease;
}

static void dhcp_send_reply(struct dhcp_server *srv,
                            const struct dhcp_msg *request,
                            u8_t message_type,
                            const ip4_addr_t *yiaddr)
{
    struct pbuf *reply_buf;
    struct dhcp_msg *reply;
    u8_t *opt;
    size_t option_bytes;
    ip_addr_t dest_ip;
    u16_t flags = request->flags;

    reply_buf = pbuf_alloc(PBUF_TRANSPORT, sizeof(struct dhcp_msg), PBUF_RAM);
    if (!reply_buf)
        return;

    reply = (struct dhcp_msg *)reply_buf->payload;
    memset(reply, 0, sizeof(struct dhcp_msg));

    reply->op = DHCP_BOOTREPLY;
    reply->htype = DHCP_HTYPE_ETH;
    reply->hlen = DHCP_HLEN_ETH;
    reply->hops = 0;
    reply->xid = request->xid;
    reply->secs = 0;
    reply->flags = flags;
    reply->ciaddr.addr = 0;
    reply->yiaddr.addr = yiaddr ? yiaddr->addr : 0;
    reply->siaddr.addr = srv->server_ip.addr;
    reply->giaddr.addr = 0;
    memcpy(reply->chaddr, request->chaddr, sizeof(reply->chaddr));
    reply->cookie = PP_HTONL(DHCP_MAGIC_COOKIE);

    opt = reply->options;
    opt = dhcp_put_option_byte(opt, DHCP_OPTION_MESSAGE_TYPE, message_type);
    opt = dhcp_put_option_ip(opt, DHCP_OPTION_SERVER_ID, &srv->server_ip);

    if (message_type == DHCP_OFFER || message_type == DHCP_ACK) {
        opt = dhcp_put_option_u32(opt, DHCP_OPTION_LEASE_TIME, srv->lease_time_s);
        opt = dhcp_put_option_ip(opt, DHCP_OPTION_SUBNET_MASK, &srv->netmask);
        if (srv->gateway.addr)
            opt = dhcp_put_option_ip(opt, DHCP_OPTION_ROUTER, &srv->gateway);
        if (srv->dns.addr)
            opt = dhcp_put_option_ip(opt, DHCP_OPTION_DNS_SERVER, &srv->dns);
    }

    *opt++ = DHCP_OPTION_END;

    option_bytes = (size_t)(opt - reply->options);
    if (option_bytes > DHCP_OPTIONS_LEN)
        option_bytes = DHCP_OPTIONS_LEN;

    pbuf_realloc(reply_buf, (u16_t)(DHCP_FIXED_LEN + option_bytes));

    if ((flags & PP_HTONS(DHCP_BROADCAST_FLAG)) ||
        message_type == DHCP_OFFER ||
        message_type == DHCP_NAK ||
        yiaddr == NULL || yiaddr->addr == 0 ||
        request->ciaddr.addr == 0) {
        IP_ADDR4(&dest_ip, 255, 255, 255, 255);
    } else {
        ip_addr_copy_from_ip4(dest_ip, *yiaddr);
    }

    udp_sendto(srv->pcb, reply_buf, &dest_ip, DHCP_CLIENT_PORT);
    pbuf_free(reply_buf);
}

static void dhcp_server_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                             const ip_addr_t *addr, u16_t port)
{
    struct dhcp_server *srv = (struct dhcp_server *)arg;
    struct dhcp_msg request;
    u16_t recv_len;
    u16_t options_len = 0;
    const u8_t *opt;
    u8_t opt_len = 0;
    u8_t msg_type = 0;
    ip4_addr_t requested_ip = {.addr = 0};
    ip4_addr_t ciaddr = {.addr = 0};
    ip4_addr_t server_id = {.addr = 0};
    struct dhcp_server_lease *lease = NULL;

    LWIP_UNUSED_ARG(pcb);
    LWIP_UNUSED_ARG(addr);
    LWIP_UNUSED_ARG(port);

    if (!srv || !p) {
        if (p)
            pbuf_free(p);
        return;
    }

    recv_len = p->tot_len;
    if (addr && IP_IS_V4(addr)) {
        const ip4_addr_t *src4 = ip_2_ip4(addr);
        printf("[dhcp] pkt from %u.%u.%u.%u:%u len=%u\n",
               ip4_addr1(src4), ip4_addr2(src4),
               ip4_addr3(src4), ip4_addr4(src4),
               port,
               recv_len);
    } else {
        printf("[dhcp] pkt from <unknown>:%u len=%u\n", port, recv_len);
    }
    if (recv_len < DHCP_FIXED_LEN) {
        printf("[dhcp] drop: short packet len=%u\n", recv_len);
        pbuf_free(p);
        return;
    }

    if (recv_len > sizeof(struct dhcp_msg))
        recv_len = sizeof(struct dhcp_msg);

    memset(&request, 0, sizeof(request));
    pbuf_copy_partial(p, &request, recv_len, 0);
    pbuf_free(p);

    if (request.op != DHCP_BOOTREQUEST ||
        request.htype != DHCP_HTYPE_ETH ||
        request.hlen != DHCP_HLEN_ETH) {
        printf("[dhcp] drop: invalid header op=%u htype=%u hlen=%u\n",
               request.op, request.htype, request.hlen);
        return;
    }

    options_len = (u16_t)(recv_len - DHCP_FIXED_LEN);

    opt = dhcp_get_option(request.options, options_len, DHCP_OPTION_MESSAGE_TYPE, &opt_len);
    if (!opt || opt_len == 0) {
        printf("[dhcp] drop: missing message type option\n");
        return;
    }
    msg_type = opt[0];

    opt = dhcp_get_option(request.options, options_len, DHCP_OPTION_REQUESTED_IP, &opt_len);
    if (opt && opt_len == 4)
        IP4_ADDR(&requested_ip, opt[0], opt[1], opt[2], opt[3]);

    opt = dhcp_get_option(request.options, options_len, DHCP_OPTION_SERVER_ID, &opt_len);
    if (opt && opt_len == 4)
        IP4_ADDR(&server_id, opt[0], opt[1], opt[2], opt[3]);

    if (request.ciaddr.addr)
        ciaddr.addr = request.ciaddr.addr;

    dhcp_server_cleanup_expired(srv);

    printf("[dhcp] recv type=%u len=%u xid=0x%08x flags=0x%04x ciaddr=%u.%u.%u.%u\n",
           msg_type,
           recv_len,
           lwip_ntohl(request.xid),
           lwip_ntohs(request.flags),
           ip4_addr1(&ciaddr), ip4_addr2(&ciaddr),
           ip4_addr3(&ciaddr), ip4_addr4(&ciaddr));

    switch (msg_type) {
    case DHCP_DISCOVER: {
        ip4_addr_t offer_ip = {.addr = 0};
        dhcp_server_log_mac_ip("discover", request.chaddr,
                               requested_ip.addr ? &requested_ip : NULL);
        lease = dhcp_server_prepare_offer(srv, request.chaddr,
                                          requested_ip.addr ? &requested_ip : NULL);
        if (!lease) {
            printf("[dhcp] discover ignored: no lease available\n");
            break;
        }
        ip4_addr_copy(offer_ip, lease->ip);
        dhcp_send_reply(srv, &request, DHCP_OFFER, &offer_ip);
        dhcp_server_emit_event(srv, DHCP_SERVER_EVENT_OFFER, &offer_ip, request.chaddr);
        break;
    }
    case DHCP_REQUEST: {
        ip4_addr_t target_ip = {.addr = 0};
        dhcp_server_log_mac_ip("request", request.chaddr,
                               requested_ip.addr ? &requested_ip : &ciaddr);

        if (server_id.addr && server_id.addr != srv->server_ip.addr) {
            printf("[dhcp] request ignored: server_id mismatch %u.%u.%u.%u\n",
                   ip4_addr1(&server_id), ip4_addr2(&server_id),
                   ip4_addr3(&server_id), ip4_addr4(&server_id));
            break;
        }

        if (requested_ip.addr)
            ip4_addr_copy(target_ip, requested_ip);
        else if (ciaddr.addr)
            ip4_addr_copy(target_ip, ciaddr);

        if (!target_ip.addr) {
            lease = dhcp_server_find_by_mac(srv, request.chaddr, 1);
            if (lease)
                ip4_addr_copy(target_ip, lease->ip);
        }

        if (!target_ip.addr || !dhcp_server_ip_in_pool(srv, &target_ip)) {
            printf("[dhcp] request nak: ip out of pool %u.%u.%u.%u\n",
                   ip4_addr1(&target_ip), ip4_addr2(&target_ip),
                   ip4_addr3(&target_ip), ip4_addr4(&target_ip));
            dhcp_send_reply(srv, &request, DHCP_NAK, NULL);
            break;
        }

        lease = dhcp_server_find_by_ip(srv, &target_ip);
        if (!lease) {
            printf("[dhcp] request nak: lease entry missing for %u.%u.%u.%u\n",
                   ip4_addr1(&target_ip), ip4_addr2(&target_ip),
                   ip4_addr3(&target_ip), ip4_addr4(&target_ip));
            dhcp_send_reply(srv, &request, DHCP_NAK, NULL);
            break;
        }

        if (lease->active && (
                !lease->mac_set || memcmp(lease->mac, request.chaddr, 6) != 0)) {
            printf("[dhcp] request nak: lease already active for %02x:%02x:%02x:%02x:%02x:%02x\n",
                   lease->mac[0], lease->mac[1], lease->mac[2],
                   lease->mac[3], lease->mac[4], lease->mac[5]);
            dhcp_send_reply(srv, &request, DHCP_NAK, NULL);
            break;
        }

        memcpy(lease->mac, request.chaddr, sizeof(lease->mac));
        lease->mac_set = 1;
        lease->active = 1;
        dhcp_server_set_expiry(srv, lease);

        dhcp_send_reply(srv, &request, DHCP_ACK, &lease->ip);
        dhcp_server_emit_event(srv, DHCP_SERVER_EVENT_ACK, &lease->ip, request.chaddr);
        break;
    }
    case DHCP_RELEASE:
        dhcp_server_log_mac_ip("release", request.chaddr,
                               ciaddr.addr ? &ciaddr : NULL);
        lease = dhcp_server_find_by_mac(srv, request.chaddr, 1);
        if (!lease && ciaddr.addr)
            lease = dhcp_server_find_by_ip(srv, &ciaddr);
        if (lease && lease->active) {
            lease->active = 0;
            lease->expiry_ms = 0;
            dhcp_server_emit_event(srv, DHCP_SERVER_EVENT_RELEASE, &lease->ip, request.chaddr);
        }
        break;
    default:
        printf("[dhcp] message type %u ignored\n", msg_type);
        break;
    }
}

static struct dhcp_server *dhcp_server_start_raw(struct netif *netif,
                                                 const ip4_addr_t *pool_start,
                                                 u32_t pool_count,
                                                 const ip4_addr_t *netmask,
                                                 const ip4_addr_t *gateway,
                                                 const ip4_addr_t *dns,
                                                 u32_t lease_time_s,
                                                 dhcp_server_event_cb event_cb,
                                                 void *event_cb_user)
{
    struct dhcp_server *srv;
    ip_addr_t bind_ip;
    u32_t i;

    if (!netif || !pool_start || !netmask || pool_count == 0)
        return NULL;

    srv = calloc(1, sizeof(*srv));
    if (!srv)
        return NULL;

    srv->leases = calloc(pool_count, sizeof(*srv->leases));
    if (!srv->leases) {
        free(srv);
        return NULL;
    }

    srv->pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!srv->pcb) {
        free(srv->leases);
        free(srv);
        return NULL;
    }

    srv->netif = netif;
    ip4_addr_copy(srv->server_ip, *netif_ip4_addr(netif));
    ip4_addr_copy(srv->netmask, *netmask);
    ip4_addr_copy(srv->pool_start, *pool_start);
    srv->pool_count = pool_count;
    srv->lease_time_s = lease_time_s ? lease_time_s : DHCP_DEFAULT_LEASE_TIME_S;
    srv->event_cb = event_cb;
    srv->event_cb_user = event_cb_user;

    ip4_addr_t pool_end;
    pool_end.addr = lwip_htonl(lwip_ntohl(pool_start->addr) + pool_count - 1U);
    printf("[dhcp] start server=%u.%u.%u.%u mask=%u.%u.%u.%u pool=%u.%u.%u.%u-%u.%u.%u.%u lease_time=%us\n",
           ip4_addr1(&srv->server_ip), ip4_addr2(&srv->server_ip),
           ip4_addr3(&srv->server_ip), ip4_addr4(&srv->server_ip),
           ip4_addr1(&srv->netmask), ip4_addr2(&srv->netmask),
           ip4_addr3(&srv->netmask), ip4_addr4(&srv->netmask),
           ip4_addr1(pool_start), ip4_addr2(pool_start),
           ip4_addr3(pool_start), ip4_addr4(pool_start),
           ip4_addr1(&pool_end), ip4_addr2(&pool_end),
           ip4_addr3(&pool_end), ip4_addr4(&pool_end),
           srv->lease_time_s);

    if (gateway)
        ip4_addr_copy(srv->gateway, *gateway);
    else
        ip4_addr_copy(srv->gateway, srv->server_ip);

    if (dns)
        ip4_addr_copy(srv->dns, *dns);
    else
        srv->dns.addr = 0;

    for (i = 0; i < pool_count; ++i) {
        u32_t addr = lwip_ntohl(pool_start->addr) + i;
        srv->leases[i].ip.addr = lwip_htonl(addr);
    }

    ip_addr_set_zero_ip4(&bind_ip);
    err_t err = udp_bind(srv->pcb, &bind_ip, DHCP_SERVER_PORT);
    if (err != ERR_OK) {
        udp_remove(srv->pcb);
        free(srv->leases);
        free(srv);
        printf("[dhcp] udp_bind failed err=%d\n", err);
        return NULL;
    }

    ip_set_option(srv->pcb, SOF_BROADCAST);
    udp_recv(srv->pcb, dhcp_server_recv, srv);

    return srv;
}

static void dhcp_server_stop_raw(struct dhcp_server *srv)
{
    if (!srv)
        return;

    if (srv->pcb)
        udp_remove(srv->pcb);
    free(srv->leases);
    free(srv);
}

struct dhcp_server_start_ctx {
    struct netif *netif;
    ip4_addr_t pool_start;
    u32_t pool_count;
    ip4_addr_t mask;
    ip4_addr_t gw;
    ip4_addr_t dns;
    u32_t lease_time_s;
    dhcp_server_event_cb event_cb;
    void *event_cb_user;
    struct dhcp_server *result;
};

struct dhcp_server_stop_ctx {
    struct dhcp_server *srv;
};

static void dhcp_server_start_cb(void *arg)
{
    struct dhcp_server_start_ctx *ctx = arg;

    ctx->result = dhcp_server_start_raw(ctx->netif,
                                        &ctx->pool_start,
                                        ctx->pool_count,
                                        &ctx->mask,
                                        &ctx->gw,
                                        &ctx->dns,
                                        ctx->lease_time_s,
                                        ctx->event_cb,
                                        ctx->event_cb_user);
}

static void dhcp_server_stop_cb(void *arg)
{
    struct dhcp_server_stop_ctx *ctx = arg;
    dhcp_server_stop_raw(ctx->srv);
}

struct dhcp_server *lwip_dhcp_server_start(struct netif *netif,
                                      const struct dhcp_server_config *config)
{
    struct dhcp_server_start_ctx ctx;
    ip4_addr_t server_ip;
    ip4_addr_t netmask_ip;
    ip4_addr_t gateway_ip;
    ip4_addr_t default_gateway_ip;
    ip4_addr_t dns_ip;
    ip4_addr_t start_ip;
    u32_t lease_count = 1;
    u32_t lease_time = DHCP_DEFAULT_LEASE_TIME_S;
    u32_t mask_host;
    u32_t server_host;
    u32_t network;
    u32_t broadcast;
    u32_t max_hosts;
    u32_t start_host;

    if (!netif)
        return NULL;

    ip4_addr_copy(server_ip, *netif_ip4_addr(netif));
    ip4_addr_copy(netmask_ip, *netif_ip4_netmask(netif));
    if (!netmask_ip.addr)
        IP4_ADDR(&netmask_ip, 255, 255, 255, 0);

    ip4_addr_copy(default_gateway_ip, *netif_ip4_gw(netif));
    if (!default_gateway_ip.addr)
        ip4_addr_copy(default_gateway_ip, server_ip);

    ip4_addr_copy(gateway_ip, default_gateway_ip);
    if (config && config->gateway) {
        if (config->gateway[0] == '\0') {
            gateway_ip.addr = 0;
        } else if (!ip4addr_aton(config->gateway, &gateway_ip)) {
            ip4_addr_copy(gateway_ip, default_gateway_ip);
        }
    }

    dns_ip.addr = 0;
    if (config && config->dns) {
        if (config->dns[0] != '\0') {
            if (!ip4addr_aton(config->dns, &dns_ip))
                ip4_addr_copy(dns_ip, default_gateway_ip);
        }
    } else {
        ip4_addr_copy(dns_ip, default_gateway_ip);
    }

    if (config && config->lease_time_seconds)
        lease_time = config->lease_time_seconds;

    if (config && config->lease_count)
        lease_count = config->lease_count;

    if (lease_count == 0)
        lease_count = 1;

    mask_host = netmask_ip.addr ? lwip_ntohl(netmask_ip.addr) : 0xFFFFFF00UL;
    server_host = lwip_ntohl(server_ip.addr);
    network = server_host & mask_host;
    broadcast = network | (~mask_host);
    if (broadcast == network)
        broadcast = network | 0x000000FFUL;

    max_hosts = (broadcast > network) ? (broadcast - network - 1U) : 0U;
    if (max_hosts == 0)
        return NULL;

    if (lease_count > max_hosts)
        lease_count = max_hosts;

    start_ip.addr = 0;
    if (config && config->lease_start && ip4addr_aton(config->lease_start, &start_ip)) {
        start_host = lwip_ntohl(start_ip.addr);
        if ((start_host & mask_host) != network ||
            start_host <= network ||
            start_host >= broadcast)
            start_ip.addr = 0;
    }

    if (!start_ip.addr) {
        start_host = server_host + 1U;
        if ((start_host & mask_host) != network ||
            start_host <= network ||
            start_host >= broadcast)
            start_host = network | 0x0000000AU;
        if (start_host >= broadcast)
            start_host = network + 1U;
        if (start_host == server_host)
            start_host++;
        start_ip.addr = lwip_htonl(start_host);
    } else {
        start_host = lwip_ntohl(start_ip.addr);
    }

    while (start_host <= network || start_host >= broadcast || start_host == server_host) {
        start_host++;
        if (start_host >= broadcast) {
            start_host = network + 1U;
            if (start_host == server_host)
                start_host++;
            break;
        }
    }

    if (start_host >= broadcast)
        start_host = broadcast - 1U;

    if (lease_count > (broadcast - start_host))
        lease_count = broadcast - start_host;

    if (lease_count == 0)
        return NULL;

    ctx.netif = netif;
    ctx.pool_start.addr = lwip_htonl(start_host);
    ctx.pool_count = lease_count;
    ip4_addr_copy(ctx.mask, netmask_ip);
    ip4_addr_copy(ctx.gw, gateway_ip);
    ip4_addr_copy(ctx.dns, dns_ip);
    ctx.lease_time_s = lease_time;
    ctx.event_cb = config ? config->event_cb : NULL;
    ctx.event_cb_user = config ? config->event_cb_user : NULL;
    ctx.result = NULL;

    tcpip_callback_with_block(dhcp_server_start_cb, &ctx, 1);

    return ctx.result;
}

void lwip_dhcp_server_stop(struct dhcp_server *srv)
{
    if (!srv)
        return;

    struct dhcp_server_stop_ctx ctx;
    ctx.srv = srv;

    tcpip_callback_with_block(dhcp_server_stop_cb, &ctx, 1);
}

const ip4_addr_t *lwip_dhcp_server_current_lease(const struct dhcp_server *srv)
{
    u32_t i;

    if (!srv)
        return NULL;

    for (i = 0; i < srv->pool_count; ++i) {
        if (srv->leases[i].active)
            return &srv->leases[i].ip;
    }

    return NULL;
}
