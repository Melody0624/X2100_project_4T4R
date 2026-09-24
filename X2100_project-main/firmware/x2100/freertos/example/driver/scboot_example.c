#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <driver/scboot.h>
#include <driver/sfc_nor.h>
#include <driver/sfc_nand.h>
#include <driver/efuse.h>

#include <include_bin.h>

// 需要替换成对应的 key.bin
INCBIN(keybin, "example/resource/scboot_key/key.bin");

// 替换对应的分区名
#define FIRMWARE_PARTITION_NAME "firmware"
#define FIRMWARE_SIZE           (1024 * 1024 * 1)

#ifdef CONFIG_SFC_NAND
static void* nand_load_firmware(char *name)
{
    int ret;
    uint32_t offset;
    uint32_t size;

    void *load_image = malloc(FIRMWARE_SIZE);
    if (!load_image) {
        printf("malloc load_image fail\n");
        return NULL;
    }

    ret = get_nand_partition_information_by_name(name, &offset, &size);
    if (ret < 0) {
        printf("get %s partition information fail!\n", name);
        return NULL;
    }

    ret = sfc_nand_flash_read_check_badblock(&offset, size, load_image);
    if (ret != size) {
        printf("RTOS nor load kernel: total write length(%d) not equal actual(%d).\n", size, ret);
        return NULL;
    }

    return load_image;
}
#endif

#ifdef CONFIG_SFC_NOR
static void* nor_load_firmware(char *name)
{
    int ret;
    uint32_t offset;
    uint32_t size;

    void *load_image = malloc(FIRMWARE_SIZE);
    if (!load_image) {
        printf("malloc load_image fail\n");
        return NULL;
    }

    ret = get_nor_partition_information_by_name(name, &offset, &size);
    if (ret < 0) {
        printf("get %s partition information fail!\n", name);
        return NULL;
    }

    ret = sfc_nor_flash_read(offset, size, load_image);
    if (ret != size) {
        printf("RTOS nor load kernel: total write length(%d) not equal actual(%d).\n", size, ret);
        return NULL;
    }

    return load_image;
}
#endif

static void* flash_load_firmware(char *name)
{
#ifdef CONFIG_SFC_NOR
    return nor_load_firmware(name);
#elif defined(CONFIG_SFC_NAND)
    return nand_load_firmware(name);
#else
    printf("RTOS: can't load partition, only support emmc_device & sfc_nand & sfc_nor!\n");
    return NULL;
#endif
}

static void test_decryption(void)
{
    uint32_t *firmware = flash_load_firmware(FIRMWARE_PARTITION_NAME);
    assert(firmware);

    int len = firmware[128];

    printf("firmware len = %d\n", len);

    secure_scboot((void *)firmware, (void *)firmware);

    printf("desryption : \n");

    int i = 0;
    for (i = 0; i < len && i < 64; i++) {
        if (i % 16 == 0)
            printf("\n");
        printf("%08x ", firmware[i]);
    }

    printf("...\n");
}

static void test_write_efuse(void)
{
    uint8_t pbit[2] = {0x55, 0xaa};

    efuse_read_segment(PROGRAM_PROTECT, pbit, 2);

    printf("read efuse PROGRAM_PROTECT = %x, %x\n", pbit[0], pbit[1]);

    efuse_read_segment(CUSTOMER_ID1, pbit, 2);

    printf("read efuse CUSTOMER_ID1 = %x, %x\n", pbit[0], pbit[1]);

    pbit[0] = 0x55;
    pbit[1] = 0xaa;

    printf("write efuse CUSTOMER_ID1 = %x, %x\n", pbit[0], pbit[1]);

    efuse_write_segment(CUSTOMER_ID1, pbit, 2);

    pbit[0] = 0;
    pbit[1] = 0;

    efuse_read_segment(CUSTOMER_ID1, pbit, 2);

    printf("read efuse CUSTOMER_ID1 = %x, %x\n", pbit[0], pbit[1]);
}

static void test_write_key(void)
{
    secure_write_key((void *)keybinData);
}

void scboot_test(void)
{
    test_write_efuse();

    test_write_key();

    test_decryption();
}