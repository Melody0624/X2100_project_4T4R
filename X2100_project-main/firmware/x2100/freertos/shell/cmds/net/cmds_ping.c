#include <common.h>

#include <lwip/opt.h>
#include <lwip/init.h>
#include <lwip/mem.h>
#include <lwip/icmp.h>
#include <lwip/netif.h>
#include <lwip/sys.h>
#include <lwip/inet.h>
#include <lwip/inet_chksum.h>
#include <lwip/ip.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <driver/systick.h>
#include <shell.h>

/** ping receive timeout - in milliseconds */
#define PING_RCV_TIMEO 1000

/** ping delay - in milliseconds */
#define PING_DELAY     1000

/** ping identifier - must fit on a u16_t */
#define PING_ID        0xAFAF

/** ping additional data size to include in the packet */
#define PING_DATA_SIZE 32

/* ping variables */
static u16_t ping_seq_num;

/** Prepare a echo ICMP request */
static void ping_prepare_echo( struct icmp_echo_hdr *iecho, u16_t len)
{
    size_t i;
    size_t data_len = len - sizeof(struct icmp_echo_hdr);

    ICMPH_TYPE_SET(iecho, ICMP_ECHO);
    ICMPH_CODE_SET(iecho, 0);
    iecho->chksum = 0;
    iecho->id     = PING_ID;
    iecho->seqno  = lwip_htons(++ping_seq_num);

    /* fill the additional data buffer with some data */
    for (i = 0; i < data_len; i++)
    {
        ((char*) iecho)[sizeof(struct icmp_echo_hdr) + i] = (char) i;
    }
    iecho->chksum = inet_chksum(iecho, len);
}

/* Ping using the socket ip */
err_t lwip_ping_send(int s, ip_addr_t *addr, int size)
{
    int err;
    struct icmp_echo_hdr *iecho;
    struct sockaddr_in to;
    int ping_size = sizeof(struct icmp_echo_hdr) + size;
    LWIP_ASSERT("ping_size is too big", ping_size <= 0xffff);

    iecho = malloc(ping_size);
    if (iecho == NULL)
    {
        return ERR_MEM;
    }

    ping_prepare_echo(iecho, (u16_t) ping_size);

    to.sin_len = sizeof(to);
    to.sin_family = AF_INET;
#if LWIP_IPV4 && LWIP_IPV6
    to.sin_addr.s_addr = addr->u_addr.ip4.addr;
#elif LWIP_IPV4
    to.sin_addr.s_addr = addr->addr;
#elif LWIP_IPV6
#error Not supported IPv6.
#endif

    err = lwip_sendto(s, iecho, ping_size, 0, (struct sockaddr*) &to, sizeof(to));
    free(iecho);

    return (err == ping_size ? ERR_OK : ERR_VAL);
}

int lwip_ping_recv(int s, int *ttl)
{
    char buf[64];
    int fromlen = sizeof(struct sockaddr_in), len;
    struct sockaddr_in from;
    struct ip_hdr *iphdr;
    struct icmp_echo_hdr *iecho;

    while ((len = lwip_recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr*) &from, (socklen_t*) &fromlen)) > 0)
    {
        if (len >= (int)(sizeof(struct ip_hdr) + sizeof(struct icmp_echo_hdr)))
        {
            iphdr = (struct ip_hdr *) buf;
            iecho = (struct icmp_echo_hdr *) (buf + (IPH_HL(iphdr) * 4));
            if ((iecho->id == PING_ID) && (iecho->seqno == lwip_htons(ping_seq_num)))
            {
                *ttl = iphdr->_ttl;
                return len;
            }
        }
    }

    return len;
}

/* using the lwIP custom ping */
int ping(char* target_name, int count, u32_t interval, u32_t size, u32_t timeout_ms)
{
    struct timeval timeout;
    int s, ttl, recv_len;
    ip_addr_t target_addr;
    u32_t send_counts;
    int recv_start_time;
    struct addrinfo hint, *res = NULL;
    struct sockaddr_in *h = NULL;
    struct in_addr ina;

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = timeout_ms % 1000 * 1000;

    send_counts = 0;
    ping_seq_num = 0;

    memset(&hint, 0, sizeof(hint));
    /* convert URL to IP */
    if (lwip_getaddrinfo(target_name, NULL, &hint, &res) != 0)
    {
        printf("ping: unknown host %s\n", target_name);
        return -1;
    }
    memcpy(&h, &res->ai_addr, sizeof(struct sockaddr_in *));
    memcpy(&ina, &h->sin_addr, sizeof(ina));
    lwip_freeaddrinfo(res);
    if (inet_aton(inet_ntoa(ina), &target_addr) == 0)
    {
        printf("ping: unknown host %s\n", target_name);
        return -1;
    }
    /* new a socket */
    if ((s = lwip_socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP)) < 0)
    {
        printf("ping: create socket failed\n");
        return -1;
    }

    lwip_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    while (1)
    {
        int elapsed_time;

        if (lwip_ping_send(s, &target_addr, size) == ERR_OK)
        {
            recv_start_time = systick_get_time_ms();
            if ((recv_len = lwip_ping_recv(s, &ttl)) >= 0)
            {
                elapsed_time = systick_get_time_ms() - recv_start_time;
                printf("%d bytes from %s icmp_seq=%d ttl=%d time=%d ms\n", recv_len, inet_ntoa(ina), send_counts,
                        ttl, elapsed_time);
            }
            else
            {
                printf("From %s icmp_seq=%d timeout\n", inet_ntoa(ina), send_counts);
            }
        }
        else
        {
            printf("Send %s - error\n", inet_ntoa(ina));
        }

        send_counts++;
        if (count > 0 && send_counts >= count)
            break;

        if (interval)
            mdelay(interval);
    }

    lwip_close(s);

    return 0;
}


void cmd_ping(struct cmd_arg *arg, int argc, char **argv)
{
    int count = 4;
    int interval = PING_DELAY;
    int size = PING_DATA_SIZE;
    int timeout_ms = PING_RCV_TIMEO;

    if (argc < 2) {
        printf("Please input: ping <host_address> [count] [interval:ms] [packetsize] [timeout:ms]\n");
        printf("    ping 192.168.1.1\n");
        printf("    ping 192.168.1.1 4 1 32 1\n");
        return;
    }

    switch (argc) {
        case 6:
            timeout_ms = atoi(argv[5]);
            if (timeout_ms <= 0) {
                printf("timeout_ms <= 0, not supported\n");
                return;
            }
        case 5:
            size = atoi(argv[4]);
            if (interval <= 0) {
                printf("interval <= 0, not supported\n");
                return;
            }
        case 4:
            interval = atoi(argv[3]);
            if (interval < 0) {
                printf("interval < 0, not supported\n");
                return;
            }
        case 3:
            count = atoi(argv[2]);
            if (count == 0) {
                printf("count == 0, not supported\n");
                return;
            }
        case 2:
            break;
        default:
            printf("Please input: ping <host_address> [count] [interval:ms] [packetsize] [timeout:ms]\n");
            return;
    }

    ping(argv[1], count, interval, size, timeout_ms);
}


void cmd_ping_init(void)
{
    shell_cmd_register(cmd_ping, "ping", NULL, "ping <host address>");
}
