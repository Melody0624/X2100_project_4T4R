#include <common.h>
#include <lwip/apps/lwiperf.h>
#include <shell.h>

static int iperf_server_start;

static void iperf_report(void *arg, enum lwiperf_report_type report_type,
    const ip_addr_t* local_addr, u16_t local_port, const ip_addr_t* remote_addr, u16_t remote_port,
    u32_t bytes_transferred, u32_t ms_duration, u32_t bandwidth_kbitpsec)
{
    printf("  0.00-%.2f sec\t %.2f MBytes  %.2f Mbits/sec\n", ((float)ms_duration/1000), ((float)bytes_transferred/1024/1024), ((float)bandwidth_kbitpsec/1024));
    printf("local %s port %d connected with %s poart %d\n", ipaddr_ntoa(local_addr), (int)local_port, ipaddr_ntoa(remote_addr), (int)remote_port);
}

void cmd_iperf_server_info(struct cmd_arg *arg, int argc, char **argv)
{
    if (!iperf_server_start) {
        iperf_server_start = 1;
        lwiperf_start_tcp_server_default(iperf_report, NULL);
        printf("iperf tcp server start\n");
        printf("-------------------------------------------------\n");
        printf("Server listening on TCP port 5001\n");
        printf("-------------------------------------------------\n");
        printf("  Interval\t Transfer\t Bandwidth\n");
    }
}

void cmd_iperf_server_init(void)
{
    shell_cmd_register(cmd_iperf_server_info, "iperf_server", NULL, "iperf_server");
}
