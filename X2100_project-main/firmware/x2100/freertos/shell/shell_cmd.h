#ifndef _SHELL_CMD_H_
#define _SHELL_CMD_H_

#ifdef CONFIG_DFS
void cmd_func_ls(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_pwd(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_mkdir(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_rm(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_cat(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_hexdump(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_echo(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_cp(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_mv(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_df(struct cmd_arg *arg, int argc, char **argv);

#ifdef CONFIG_OS_MODULE
void cmd_func_insmod(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_lsmod(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_rmmod(struct cmd_arg *arg, int argc, char **argv);
#endif

#ifdef DFS_USING_WORKDIR
void cmd_func_cd(struct cmd_arg *arg, int argc, char **argv);
#endif

#endif

#ifdef CONFIG_LRZSZ
extern void cmd_lrzsz_init(void);
#endif /* end of CONFIG_LRZSZ */

#ifdef CONFIG_OS
extern void cmd_wake_lock_init(void);
void cmd_func_thread_list(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_top(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_thread_dump(struct cmd_arg *arg, int argc, char **argv);
#endif

#ifdef CONFIG_PM
void cmd_pm_init(void);
#endif

#ifdef CONFIG_SFC_NOR
extern void cmd_nor_init(void);
extern void cmd_update_nor_init(void);
#endif

#ifdef CONFIG_CAMERA
extern void cmd_camera_init(void);
#endif

#ifdef CONFIG_ISP
extern void cmd_isp_init(void);
#endif

#ifdef CONFIG_I2C
extern void cmd_i2c_init(void);
#endif

#ifdef CONFIG_ADC
extern void cmd_adc_init(void);
#endif

#ifdef CONFIG_WDT
extern void cmd_reset_init(void);
#endif

#ifdef CONFIG_EFUSE
extern void cmd_efuse_init(void);
#endif

#ifdef CONFIG_HASH
extern void cmd_hash_init(void);
#endif

#ifdef CONFIG_GPIO
extern void cmd_gpio_init(void);
#endif

#ifdef CONFIG_PWM
extern void cmd_pwm_init(void);
#endif

#ifdef CONFIG_RTC
extern void cmd_rtc_init(void);
#endif

#ifdef CONFIG_SPI
extern void cmd_spi_init(void);
#endif

#ifdef CONFIG_MEMTESTER
extern void cmd_memtester_init(void);
#endif

#ifdef CONFIG_LIBIAAC
extern void cmd_iaat_init(void);
#endif

#ifdef CONFIG_SFC_NAND
extern void cmd_nand_init(void);
extern void cmd_update_nand_init(void);
#endif

#ifdef CONFIG_EMMC_DEVICE
extern void cmd_mmc_init(void);
extern void cmd_update_mmc_init(void);
#endif

#ifdef CONFIG_NET_LWIP
extern void cmd_ping_init(void);
extern void cmd_mac_info_init(void);

#ifdef CONFIG_NET_LWIP_APPS
void cmd_tftp_init(void);
void cmd_mdns_init(void);
void cmd_iperf_server_init(void);
#ifdef CONFIG_DFS
#endif /* CONFIG_DFS */
#endif /* CONFIG_NET_LWIP_APPS */

#ifdef CONFIG_DEVICES_WIRELESS
void cmd_wireless_interactive_init(void);
#endif

#endif /* CONFIG_NET_LWIP */

void cmd_func_history(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_help(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_memcpy(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_memclear(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_memset(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_memdump(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_devmem(struct cmd_arg *arg, int argc, char **argv);
void cmd_func_free(struct cmd_arg *arg, int argc, char **argv);

#endif /* _SHELL_CMD_H_ */
