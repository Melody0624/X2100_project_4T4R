#include "lwip/apps/mdns.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <shell.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

void mdns_usage(void)
{
    printf("Usage: \n");
    printf("      mdns <hostname> \n");
}

void mdns_init(struct netif *netif, const char *hostname)
{
    err_t res;
    mdns_resp_init();
    res = mdns_resp_add_netif(netif, hostname);
    LWIP_ERROR("add netif failed", (res == ERR_OK), return);
}

void cmd_func_mdns(struct cmd_arg *arg, int argc, char **argv) {
    if (!strcmp(argv[1], "-h") || argc != 2)
    {
        mdns_usage();
        return;
    }
    struct netif *netif = netif_default;
    mdns_init(netif,argv[1]);
    return;
}

void cmd_mdns_init(void)
{
    shell_cmd_register(cmd_func_mdns, "mdns", NULL, "mdns");
}