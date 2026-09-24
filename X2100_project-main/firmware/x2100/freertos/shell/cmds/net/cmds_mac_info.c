#include <common.h>
#include <lwip/netif.h>
#include <lwip/inet.h>
#include <shell.h>

void cmd_mac_info(struct cmd_arg *arg, int argc, char **argv)
{
    int i;
    struct netif *netif;

    NETIF_FOREACH(netif) {
        printf("network interface device: %c%c%d%s\n", netif->name[0], netif->name[1],
                netif->num, (netif == netif_default) ? " (Default)" : "");

        printf("  MTU: %d\n", netif->mtu);

        printf("  MAC: ");
        for (i = 0; i < netif->hwaddr_len; i++)
            printf("%02x ", netif->hwaddr[i]);
        printf("\n");

        printf("  FLAGS:");
        printf(" %s", netif_is_up(netif) ? "UP" : "DOWN");
        printf(" %s", netif_is_link_up(netif) ? "LINK_UP" : "LINK_DOWN");
        if (netif_is_flag_set(netif, NETIF_FLAG_ETHARP)) printf(" ETHARP");
        if (netif_is_flag_set(netif, NETIF_FLAG_BROADCAST)) printf(" BROADCAST");
        if (netif_is_flag_set(netif, NETIF_FLAG_IGMP)) printf(" IGMP");
        printf("\n");

        printf("  ip address: %s\n", inet_ntoa(netif->ip_addr));
        printf("  gw address: %s\n", inet_ntoa(netif->gw));
        printf("  net mask  : %s\n", inet_ntoa(netif->netmask));
  }

}


void cmd_mac_info_init(void)
{
    shell_cmd_register(cmd_mac_info, "mac_info", NULL, "mac_info");
}
