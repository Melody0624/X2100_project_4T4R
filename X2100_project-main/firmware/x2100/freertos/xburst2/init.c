#include "asm/mipsregs.h"
#include "common.h"
#include "lds_symbol.h"
#include "asm_symbol.h"
#include "irqflags.h"
#include "asm/addrspace.h"
#include "spinlock.h"
#include <driver/cache.h>
#include <driver/irq.h>
#include <stdint.h>
#include <driver/clk.h>
#include <__ffs.h>
#include <driver/uart.h>
#include <string.h>
#include <driver/systick.h>
#include <driver/uart_console.h>
#include <driver/console.h>
#include <driver/fb.h>
#include <driver/hrtimer.h>
#include <driver/dma.h>
#include <cpu/cpu.h>
#include <os.h>
#include <driver/spi.h>
#include <driver/sslv.h>
#include <driver/i2c.h>
#include <driver/rtc.h>
#include <driver/pwm.h>
#include <driver/efuse.h>
#include <driver/aes.h>
#include <driver/sfc_nor.h>
#include <driver/sfc_nand.h>
#include <driver/pm.h>
#include <usb/usb.h>
#include <driver/camera.h>
#include <driver/rotator.h>
#include <module.h>
#include <show_compile_time.h>
#include <driver/backlight.h>
#include <driver/gpio.h>
#include <driver/gpio_pin.h>
#include <filesystem/filesystem_init.h>
#include <driver/mscaler.h>
#include <driver/dtrng.h>
#include <driver/des.h>
#include <driver/conn.h>
#include <driver/ramdisk.h>
#include <driver/mmc.h>
#include <driver/can.h>
#include <spl_rtos_argument.h>
#include <lib/spl_cmdargs_analysis.h>
#include <driver/inherit.h>
#include <driver/pwm_scanner.h>

#ifdef CONFIG_NET_LWIP
#include <lwip/tcpip.h>
#endif

#include <driver/mac.h>

#ifdef CONFIG_FPU_ALWAYS_ON
#error "xburst2 not support fpu always at this time, please support it"
#endif

void vendor_init(void *arg);
void soc_extra_device_init(void);
void __libc_init_array(void);
void init_ctors(void);
void soc_late_init(void);
void ilcdtools_lcd_init(void);
__weak void soc_export_config_for_linux(void)
{
    printf("soc_export_config_for_linux() not implemented for this soc.\n");
}

#ifdef CONFIG_APPLICATION
void applicetion_init(void *arg);
#endif

#ifdef CONFIG_DEVICES
void external_devices_init(void);
#endif

#ifdef CONFIG_DFS
extern void dfs_device_init(void);
#endif

#ifdef CONFIG_SHELL
int shell_init(void);
#endif

#ifdef CONFIG_USB_IS_HOST
extern int usb_host_init(void);
extern void usb_hub_start(void);
#endif

#ifdef CONFIG_TCSM_SECTION
    void soc_tcsm_section_init(void);
#endif

struct zero *zero;

void console_thread(void *data)
{
    char ch;
    int is_r = 0;
    while (1) {
        ch = console_get_char();

        if (ch == '\r') {
            printf("\n");
            printf("");
        } else if (ch == '\n' && !is_r) {
            printf("\n");
            printf("");
        } else {
            printf("%c", ch);
        }

        is_r = ch == '\r';
    }
}

static void init_drivers(void *data)
{
    init_ctors();

#ifdef CONFIG_GPIO
    gpio_voltage_init();
#endif

#ifdef CONFIG_DMA
    dma_init();
#endif

#ifdef CONFIG_PWM
    pwm_init();
#endif

#ifdef CONFIG_ROTATOR
    rotator_init();
#endif

#ifdef CONFIG_SPI
    spi_init();
#endif

#ifdef CONFIG_AES
    aes_init();
#endif

#ifdef CONFIG_SSLV
    sslv_init();
#endif

#ifdef CONFIG_I2C
    i2c_init();
#endif

#ifdef CONFIG_RTC
    rtc_init();
#endif

#ifdef CONFIG_EFUSE
    efuse_init();
#endif

#ifdef CONFIG_CAN
    can_init();
#endif

#ifdef CONFIG_DFS
    dfs_device_init();
#endif

#ifdef CONFIG_SFC_NOR
    sfc_nor_flash_init();
#endif

#ifdef CONFIG_SFC_NAND
    sfc_nand_flash_init();
#endif

#ifdef CONFIG_RAMDISK
    ramdisk_devices_init();
#endif

#ifdef CONFIG_MMC
    mmc_init(data);
#endif

/**
 * inherit_init传的参数分别是mem,size
 * 当mem为NULL的时候，默认使用share_mem作为配置区存放要继承的数据;
 * 当mem!=NULL且size足够大的时候，并且linux和rtos能够同时访问，要继承的数据存放在此处。
 * 比如tcsm区域,调用inherit_init时传入tcsm的起始地址和大小，继承方和被继承方需保持一致。
 */
#ifdef CONFIG_MEMORY_INHERIT
    inherit_init(NULL, 0);
#endif

#ifdef CONFIG_CAMERA
    camera_init();
#endif

#ifdef CONFIG_DTRNG
    dtrng_init();
#endif

#ifdef CONFIG_HASH
    hash_init();
#endif

#ifdef CONFIG_DES
    des_init();
#endif

#ifdef CONFIG_PWM_SCANNER
    pwm_scanner_set_config(CONFIG_PWM_SCANNER_INPUT_FMT, CONFIG_HSYNC_GPIO);
    pwm_scanner_init();
#endif

    soc_extra_device_init();

#ifdef CONFIG_DEVICES
    external_devices_init();
#endif

#ifdef CONFIG_LCD_ILCDTOOLS
    ilcdtools_lcd_init();
#endif

#ifdef CONFIG_FB
    fb_init();
#endif

    soc_late_init();
}

#ifdef CONFIG_OS
static void main_init_thread(void *data)
{
    init_drivers(data);

#ifdef CONFIG_NET_LWIP
    tcpip_init(NULL,NULL);
#endif

#ifdef CONFIG_MAC
    mac_init();
#endif

#ifdef CONFIG_DFS
    file_system_init();
#endif

#ifdef CONFIG_OS_MODULE
    module_param_sysfs_init();
#endif

#ifdef CONFIG_USB_IS_HOST
    usb_host_init();
#endif

#ifdef CONFIG_USB_DRIVER
    usb_core_init();
#endif

#ifdef CONFIG_USB_IS_HOST
    usb_hub_start();
#endif

#ifdef CONFIG_SHELL
    shell_init();
#else
    thread_create("console thread", 8192, console_thread, NULL);
#endif

#ifdef CONFIG_APPLICATION
    applicetion_init(data);
#endif

    vendor_init(data);

/* c++ std iostream 会用到 此线程的 stdout/stdio/stderr 变量,
 * 所以此线程不能退出
 */
#if defined(CONFIG_LIBCXX) || defined(CONFIG_LIBSTDCPP)
    while (1)
        thread_wait();
#endif
}
#endif

void c_main(void *data)
{
    zero = (void *)&_start;

    arch_init_cpu();

    arch_enable_fpu();

#ifdef __mips_msa
    arch_enable_simd();
#endif

#ifdef CONFIG_UART_CONSOLE
    uart_console_init();
#endif

#ifdef CONFIG_SHOW_COMPILE_TIME
    show_compile_time();
#endif

#ifdef CONFIG_CACHE
    cache_init();
#endif

#ifdef CONFIG_TCSM_SECTION
    soc_tcsm_section_init();
#endif

#ifdef CONFIG_IRQ
    irq_init();
#endif

#ifdef CONFIG_NEWLIB
    __libc_init_array();
#endif

#ifdef CONFIG_SYSTICK
    systick_init();
#endif

#ifdef CONFIG_CLK
    init_all_clk();
#endif

#if defined(DEBUG) && defined(CONFIG_CLK)
    clocks_show();
#endif

#ifdef CONFIG_PM
    pm_init();
#endif

#ifdef CONFIG_HRTIMER
    hrtimer_core_init();
#endif

#ifdef CONFIG_CONN
    conn_init();
#endif

    cmdargs_mem_info_anlysis(data);

#ifdef CONFIG_OS
    thread_create("main init thread", 65535, main_init_thread, data);

    os_start_scheduler();
#else
    init_drivers(data);
    console_thread(NULL);
#endif
}
