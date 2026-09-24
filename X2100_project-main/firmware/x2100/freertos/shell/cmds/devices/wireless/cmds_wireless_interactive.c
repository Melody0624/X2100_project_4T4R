#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <shell.h>
#include <dfs.h>
#include <common.h>

#ifdef CONFIG_WIRELESS_ATBM
extern void atbm_wifi_app_cmd(struct cmd_arg *arg,int argc, char **args);
//extern void atbm_os_api_test(struct cmd_arg *arg,int argc, char **args);
extern void atbm_wifi_test(struct cmd_arg *arg,int argc, char **args);
extern void iperf_tool(int argc, char **argv);
#endif

void cmd_wireless_interactive_init(void)
{
#ifdef CONFIG_WIRELESS_ATBM
/*
 * 1. STA 模式测试
 * atbmwifi mode sta
 * atbmwifi scan
 * atbmwifi join sw1 #sz@sw1^
 *
 * mac_info  // 查看分配到的IP信息
 *
 * 2. AP 模式测试
 * atbmwifi mode ap
 * atbmwifi ap atbm 12345678
 * 说明：
 *      atbm6031使用以上命令默认开启2.4G，且atbm6031仅支持2.4G
 *      atbm6132使用以上命令默认开启5G，如使用2.4G可使用以下命令：
 *      atbmwifi ap atbm 12345678 2.4G
 *      atbm6132也可使用以下命令开启5G：
 *      atbmwifi ap atbm 12345678 5G
 *
 *      atbm6132如果需要切换频段(2.4G->5G或者5G->2.4G)，需要使用以下命令停止AP再重新连接：
 *      atbmwifi ap_stop
 *      atbmwifi ap atbm 12345678 2.4G/5G
 *
 * 手机连接即可
 *
 * 3. P2P模式测试
 * atbmwifi mode p2p
 * atbmwifi p2p_start
 * atbmwifi p2p_go
 * 手机端 找到 设置---> 高级设置---> WLAN直连 即可
 *
 * 4. 蓝牙模式测试
 * atbmwifi ble_test AT+SMT_START
 * 使用手机软件"nRF Connect"进行连接和数据读写测试，或者找高拓支持提供专门的测试小程序进行蓝牙配网测试
 */
    shell_cmd_register(atbm_wifi_app_cmd,"atbmwifi",NULL,"atbm wifi app cmd");
    //shell_cmd_register(atbm_os_api_test,"atbm_func_test",NULL,"os api test");
    //shell_cmd_register(atbm_wifi_test,"atbmwifi_p2p",NULL,"atbm wifi test cmd");
    //shell_cmd_register(iperf_tool,"iperf",NULL,"atbm wifi iperf cmd");
#endif
}
