/**
 * rsa3072_boot_kernel_example
 *
 * 本示例与 `quick_boot_logo_and_boot_kernel_example.c` 类似：去掉了屏幕显示相关代码，
 * 主要用于验证替代 SPL 阶段的 RSA3072 验签流程。
 *
 * 代码流程：
 * 1) 从 SPL 传入的 `spl_rtos_argument` 中取得 `rtos_boot_os_args`（magic 为 'ARGS'）；
 * 2) 根据存储介质配置（EMMC/SFC_NAND/SFC_NOR）加载 kernel 到 `args->load_addr`；
 * 3) 可选：在跳转 Linux 前执行安全启动相关的验签/解密流程（见下方配置项说明）；
 * 4) 释放中断/CPU 等资源后，调用 `jump_to_image_linux()` 跳转启动 Linux。
 *
 * 可选配置说明（按需使能）：
 * - CONFIG_SCBOOT：开启 secureboot 框架；关闭时不会调用 `secure_scboot()`。
 * - CONFIG_SECURE_BOOT_KERNEL：在 CONFIG_SCBOOT 打开时，对 kernel 做“验签+解密”。
 * - CONFIG_CHECK_SIG_ROOTFS：在 CONFIG_SCBOOT 打开时，对 rootfs 做验签/哈希校验；
 *   本示例默认使用 `args->sig_buf` 作为签名/头部缓冲区，并从 `args->rootfs_offset` 读取 rootfs 数据（当前实现走 SFC NAND 读取，
 *   若 rootfs 位于其它介质请自行替换读取接口）。
 * - CONFIG_RSA3072：选择 RSA-3072 进行验签（未使能时默认按 RSA-2048 处理，具体以 scboot 实现为准）。
 */

#include <stdio.h>
#include <common.h>
#include <os.h>
#include <driver/cache.h>
#include <spl_rtos_argument.h>
#include <uboot_lib.h>
#include <driver/scboot.h>

/******************************load_kernel******************************/

static inline void *is_address_valid(unsigned long address)
{
    if (address > CKSEG0 && address < CKSEG2)
        return (void *)address;
    else
        return NULL;
}

void sfc_load_something(void)
{
    /* 加载某些东西 */
}

#ifdef CONFIG_EMMC_DEVICE

#define MMC_SECTOR_SIZE                 (512)

uint32_t mmc_device_block_read(uint64_t address, uint32_t length, void *buffer);

static int mmc_load_kernel(struct rtos_boot_os_args *args)
{
    int size_sector = (args->size + MMC_SECTOR_SIZE - 1) / MMC_SECTOR_SIZE;
    int size = size_sector * MMC_SECTOR_SIZE;

    int ret = mmc_device_block_read(args->offset, size, (void *)args->load_addr);
    if (ret < 0) {
        printf("RTOS mmc load kernel failed.\n");
        ret = -EACCES;
    }

    return ret;
}
#endif

#ifdef CONFIG_SFC_NAND
#include <driver/sfc_nand.h>
static int nand_load_kernel(struct rtos_boot_os_args *args)
{
    uint32_t offset = args->offset;
    int ret = sfc_nand_flash_read_check_badblock(&offset, args->size, (uint8_t *)args->load_addr);
    if (ret != args->size) {
        printf("RTOS nand load kernel: total write length(%d) not equal actual(%d).\n", args->size, ret);
        ret = -EACCES;
    }

    return ret;
}
#endif

#ifdef CONFIG_SFC_NOR
#include <driver/sfc_nor.h>
static int nor_load_kernel(struct rtos_boot_os_args *args)
{
    int ret = sfc_nor_flash_read(args->offset, args->size, (uint8_t *)args->load_addr);
    if (ret != args->size) {
        printf("RTOS nor load kernel: total write length(%d) not equal actual(%d).\n", args->size, ret);
        ret = -EACCES;
    }

    return ret;
}
#endif

#ifdef CONFIG_CHECK_SIG_ROOTFS
void secure_check_hash_rootfs(void *buffer, uint8_t *rootfs_offset, unsigned int ram_size)
{
	unsigned int code_len;
	unsigned int *ptr = (unsigned int *)buffer;
	unsigned int rootfs_load_addr = (unsigned int)buffer + 2048;
	int ret;

	if (rootfs_offset == -1) {
		printf("rootfs partitions not found\n");
		hang();
	}

	code_len = ptr[128];
	if ((virt_to_phys(rootfs_load_addr) + code_len) >  ram_size) {
		printf("rootfs load add + size exceed ram size, please check load rootfs addr and size!!!\n");
		hang();
	}

	sfc_nand_flash_read(rootfs_offset, code_len, rootfs_load_addr);

	ret = secure_scboot((void *)buffer, (void *)rootfs_load_addr);
	if(ret) {
		printf("Error check rootfs hash.\n");
		hang();
	}
}
#endif

int load_kernel_partition(struct rtos_boot_os_args *args)
{
    int ret;
#ifdef CONFIG_EMMC_DEVICE
    ret = mmc_load_kernel(args);
#elif defined(CONFIG_SFC_NAND)
    ret = nand_load_kernel(args);
#elif defined(CONFIG_SFC_NOR)
    ret = nor_load_kernel(args);
#else
    printf("RTOS: can't load kernel image, only support emmc_device & sfc_nand & sfc_nor!\n");
    return -EACCES;
#endif

#ifdef CONFIG_SCBOOT
#ifdef CONFIG_CHECK_SIG_ROOTFS
    /* signature address */
	secure_check_hash_rootfs((void *)args->sig_buf,(uint8_t *)args->rootfs_offset, args->ram_size);
#endif

#ifdef CONFIG_SECURE_BOOT_KERNEL
    ret = secure_scboot((void *)args->load_addr, (void *)args->load_addr);
	if(ret) {
		printf("Error spl secure load kernel.\n");
		hang();
	}
#endif
#endif
    return ret;
}

/******************************release******************************/
#include <driver/irq.h>
#include <cpu/cpu.h>

static void release_all_resources(void)
{
    release_all_irq();

    arch_deinit_cpu();
}

void rsa3072_boot_kernel_example(void *arg)
{
    struct spl_rtos_argument *spl_argument = (struct spl_rtos_argument *)arg;
    struct rtos_boot_os_args *os_args = NULL;
    void *data = NULL;
    if (is_address_valid((unsigned long)spl_argument))
        data = (void *)spl_argument->os_boot_args;

    /* 参数有效性检查 */
    if (is_address_valid((unsigned long)data)) {
        if (((struct rtos_boot_os_args *)data)->magic == 0x53475241)
            os_args = (struct rtos_boot_os_args *)data;
    }

    if (!os_args) {
        printf("spl argument boot is args is NULL\n");
        return;
    }

    sfc_load_something();

    int ret = load_kernel_partition(os_args);
    if (ret < 0)
        panic("RTOS: load kernel failed!!!\n");

    release_all_resources();

    jump_to_image_linux(data);
}
