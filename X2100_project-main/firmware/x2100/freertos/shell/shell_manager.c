#include <common.h>
#include <shell.h>
#include "shell_cmd.h"

extern void shell_thread_func(void *data);
extern void shell_cmd_register(cmd_func_t cmd_fn, char *name, char *args, char *help);

static void shell_cmd_list_init(void)
{
    shell_cmd_register(cmd_func_memcpy,     "memcpy",   NULL, "memory copy");
    shell_cmd_register(cmd_func_memclear,   "memclear", NULL, "memory clear");
    shell_cmd_register(cmd_func_memset,     "memset",   NULL, "memory set");
    shell_cmd_register(cmd_func_memdump,    "memdump",  NULL, "memory dump");
    shell_cmd_register(cmd_func_history,    "history",  NULL, "show the commands history");
    shell_cmd_register(cmd_func_help,       "help",     NULL, NULL);
    shell_cmd_register(cmd_func_devmem,     "devmem",   NULL, "read/write register");
    shell_cmd_register(cmd_func_free,       "free",     NULL, "show mem info");

#ifdef CONFIG_DFS
    shell_cmd_register(cmd_func_ls,         "ls",       NULL, "list directory contents");
    shell_cmd_register(cmd_func_pwd,        "pwd",      NULL, "print name of current/working directory");
  #ifdef DFS_USING_WORKDIR
    shell_cmd_register(cmd_func_cd,         "cd",       NULL, "change current directory");
  #endif
    shell_cmd_register(cmd_func_mkdir,      "mkdir",    "-p", "create new directory");
    shell_cmd_register(cmd_func_rm,         "rm",       "-r", "delete file/directory");
    shell_cmd_register(cmd_func_cat,        "cat",      NULL, "show file content");
    shell_cmd_register(cmd_func_hexdump,    "hexdump",  NULL, "special way to show file content");
    shell_cmd_register(cmd_func_echo,       "echo",     NULL, "echo string to file");
    shell_cmd_register(cmd_func_cp,         "cp",       NULL, "copy files and directories");
    shell_cmd_register(cmd_func_mv,         "mv",       NULL, "move (rename) files");
    shell_cmd_register(cmd_func_df,         "df",       NULL, "report file system disk space usage");
  #ifdef CONFIG_OS_MODULE
    shell_cmd_register(cmd_func_insmod,     "insmod",   NULL, "insert a module into the Kernel");
    shell_cmd_register(cmd_func_lsmod,      "lsmod",    NULL, "Show the status of modules");
    shell_cmd_register(cmd_func_rmmod,      "rmmod",    NULL, "remove a module from the Kernel");
  #endif /* end of CONFIG_OS_MODULE */

#endif /* end of CONFIG_DFS */

#ifdef CONFIG_LRZSZ
  cmd_lrzsz_init();
#endif /* end of CONFIG_LRZSZ */

#ifdef CONFIG_OS
  shell_cmd_register(cmd_func_thread_list,  "thread_list",  NULL,       "list view the current processes");
  shell_cmd_register(cmd_func_top,          "top",          "-d -t -n", "display os processes info");
  shell_cmd_register(cmd_func_thread_dump,  "thread_dump",  NULL,       "Print the calling process of a running thread");
  cmd_wake_lock_init();
#endif

#ifdef CONFIG_PM
  cmd_pm_init();
#endif

#ifdef CONFIG_SPI
    cmd_spi_init();
#endif

#ifdef CONFIG_SFC_NOR
    cmd_nor_init();
    cmd_update_nor_init();
#endif

#ifdef CONFIG_GPIO
    cmd_gpio_init();
#endif

#ifdef CONFIG_CAMERA
    cmd_camera_init();
#endif

#ifdef CONFIG_ISP
    cmd_isp_init();
#endif

#ifdef CONFIG_PWM
    cmd_pwm_init();
#endif

#ifdef CONFIG_EFUSE
    cmd_efuse_init();
#endif

#ifdef CONFIG_HASH
    cmd_hash_init();
#endif

#ifdef CONFIG_RTC
    cmd_rtc_init();
#endif

#ifdef CONFIG_I2C
    cmd_i2c_init();
#endif

#ifdef CONFIG_ADC
    cmd_adc_init();
#endif

#ifdef CONFIG_WDT
    cmd_reset_init();
#endif

#ifdef CONFIG_MEMTESTER
    cmd_memtester_init();
#endif

#ifdef CONFIG_LIBIAAC
    cmd_iaat_init();
#endif

#ifdef CONFIG_SFC_NAND
    cmd_nand_init();
    cmd_update_nand_init();
#endif

#ifdef CONFIG_EMMC_DEVICE
    cmd_mmc_init();
    cmd_update_mmc_init();
#endif

#ifdef CONFIG_NET_LWIP
    cmd_ping_init();
    cmd_mac_info_init();
#ifdef CONFIG_NET_LWIP_APPS
    cmd_tftp_init();
    cmd_mdns_init();
    cmd_iperf_server_init();
#ifdef CONFIG_DFS
#endif /* CONFIG_DFS */

#endif /* CONFIG_NET_LWIP_APPS */

#ifdef CONFIG_DEVICES_WIRELESS
    cmd_wireless_interactive_init();
#endif

#endif /* end of CONFIG_NET_LWIP */

}

static void console_shell_init(void)
{
    thread_ptr_t thread;

    thread = thread_create("shell", 1024 * 12,
            (thread_func_t)shell_thread_func, NULL);

    thread_set_priority(thread, OS_priority_normal);
}

int shell_init(void)
{
    shell_cmd_list_init();

    console_shell_init();

    return 0;
}
