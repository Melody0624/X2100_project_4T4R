src-y += shell.c
src-y += shell_manager.c
src-y += shell_cmd.c

src-y += cmds/cmds_help.c
src-y += cmds/cmds_history.c
src-y += cmds/cmds_mem.c
src-y += cmds/cmds_devmem.c
src-y += cmds/cmds_free.c

src-$(CONFIG_DFS) += cmds/cmds_ls.c
src-$(CONFIG_DFS) += cmds/cmds_pwd.c
src-$(CONFIG_DFS) += cmds/cmds_cd.c
src-$(CONFIG_DFS) += cmds/cmds_mkdir.c
src-$(CONFIG_DFS) += cmds/cmds_rm.c
src-$(CONFIG_DFS) += cmds/cmds_cat.c
src-$(CONFIG_DFS) += cmds/cmds_hexdump.c
src-$(CONFIG_DFS) += cmds/cmds_echo.c
src-$(CONFIG_DFS) += cmds/cmds_cp.c
src-$(CONFIG_DFS) += cmds/cmds_mv.c
src-$(CONFIG_DFS) += cmds/cmds_df.c
src-$(CONFIG_DFS) += cmds/cmds_insmod.c
src-$(CONFIG_DFS) += cmds/cmds_lsmod.c
src-$(CONFIG_DFS) += cmds/cmds_rmmod.c

src-$(CONFIG_NET_LWIP) += cmds/net/cmds_ping.c
src-$(CONFIG_NET_LWIP) += cmds/net/cmds_mac_info.c
src-$(CONFIG_NET_LWIP_APPS) += cmds/net/cmd_tftp.c
src-$(CONFIG_NET_LWIP_APPS) += cmds/net/cmd_mdns.c
src-$(CONFIG_NET_LWIP_APPS) += cmds/net/cmds_iperf_server.c

# driver cmds
src-$(CONFIG_CAMERA) += cmds/drivers/cmds_camera.c
src-$(CONFIG_ISP) += cmds/drivers/cmds_isp.c
src-$(CONFIG_PWM) += cmds/drivers/cmds_pwm.c
src-$(CONFIG_SFC_NOR) += cmds/drivers/cmds_nor.c cmds/drivers/cmd_update_nor.c
src-$(CONFIG_SFC_NAND) += cmds/drivers/cmd_update_nand.c cmds/drivers/cmds_nand.c
src-$(CONFIG_EMMC_DEVICE) += cmds/drivers/cmd_update_mmc.c cmds/drivers/cmds_mmc.c
src-$(CONFIG_SPI) += cmds/drivers/cmds_spi.c
src-$(CONFIG_RTC) += cmds/drivers/cmds_rtc.c
src-$(CONFIG_I2C) += cmds/drivers/cmds_i2c.c
src-$(CONFIG_ADC) += cmds/drivers/cmds_adc.c
src-$(CONFIG_WDT) += cmds/cmds_reset.c
src-$(CONFIG_HASH) += cmds/drivers/cmds_hash.c

# driver/gpio cmds
ifeq ($(CONFIG_GPIO),y)
src-$(CONFIG_SOC_X1830S) += cmds/drivers/gpio/cmds_x1830_gpio.c
src-$(CONFIG_SOC_X1520) += cmds/drivers/gpio/cmds_x1520_gpio.c
src-$(CONFIG_SOC_X1021) += cmds/drivers/gpio/cmds_x1021_gpio.c
src-$(CONFIG_SOC_X1000S) += cmds/drivers/gpio/cmds_x1000_gpio.c
src-$(CONFIG_SOC_X2000) += cmds/drivers/gpio/cmds_x2000_gpio.c
src-$(CONFIG_SOC_X1600) += cmds/drivers/gpio/cmds_x1600_gpio.c
src-$(CONFIG_SOC_X2600) += cmds/drivers/gpio/cmds_x2600_gpio.c
src-$(CONFIG_SOC_AD100) += cmds/drivers/gpio/cmds_ad100_gpio.c
endif

# driver/efuse cmds
ifeq ($(CONFIG_EFUSE),y)
src-$(CONFIG_SOC_X1830S) += cmds/drivers/efuse/cmds_x1830_efuse.c
src-$(CONFIG_SOC_X1520) += cmds/drivers/efuse/cmds_x1520_efuse.c
src-$(CONFIG_SOC_X1021) += cmds/drivers/efuse/cmds_x1021_efuse.c
src-$(CONFIG_SOC_X1000S) += cmds/drivers/efuse/cmds_x1000_efuse.c
src-$(CONFIG_SOC_X1600) += cmds/drivers/efuse/cmds_x1600_efuse.c
src-$(CONFIG_SOC_X2000) += cmds/drivers/efuse/cmds_x2000_efuse.c
src-$(CONFIG_SOC_X2600) += cmds/drivers/efuse/cmds_x2600_efuse.c
src-$(CONFIG_SOC_AD100) += cmds/drivers/efuse/cmds_ad100_efuse.c
endif

# os cmds
src-$(CONFIG_OS) += cmds/os/cmds_wake_lock.c
src-$(CONFIG_OS)  += cmds/cmds_thread_list.c
src-$(CONFIG_OS)  += cmds/cmds_top.c
src-$(CONFIG_OS)  += cmds/cmds_thread_dump.c

# pm cmds
src-$(CONFIG_PM) += cmds/cmds_pm.c

# devices (wireless)cmds
src-$(CONFIG_DEVICES_WIRELESS) += cmds/devices/wireless/cmds_wireless_interactive.c

# third_party
src-$(CONFIG_MEMTESTER)  += cmds/cmds_memtester.c
src-$(CONFIG_LRZSZ)  += cmds/cmds_lrzsz.c
