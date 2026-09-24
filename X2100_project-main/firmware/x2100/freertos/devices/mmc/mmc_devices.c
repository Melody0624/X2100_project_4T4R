/*
 * Copyright (c) 2019, Ingenic Semiconductor
 *
 */
#include <common.h>
#include "mmc_devices.h"
#include "mmc_partition.h"

#ifdef CONFIG_EMMC_DEVICE
extern struct mmc_devices_config emmc_config;
#endif

__attribute__((__unused__)) static int mmc_device_block_test(void);

static struct mmc_card *devce_card;
static struct mutex mmc_mutex;

int mmc_devices_init(void)
{
#ifdef CONFIG_EMMC_DEVICE
    devce_card = mmc_register_device(&emmc_config);

#ifdef CONFIG_DFS_ELMFAT
    if (devce_card)
        emmc_device_partition_init(devce_card);
#endif

#endif
    mutex_init(&mmc_mutex);
    //mmc_device_block_test();
    return 0;
}

int mmc_devices_deinit(void)
{
#ifdef CONFIG_EMMC_DEVICE

#ifdef CONFIG_DFS_ELMFAT
    if (devce_card)
        emmc_device_partition_deinit(devce_card);
#endif

    mmc_unregister_device(&emmc_config);

    devce_card = NULL;
#endif
    return 0;
}

struct mmc_card *mmc_devices_info(void)
{
    return devce_card;
}

uint32_t mmc_device_block_read(uint64_t address, uint32_t length, void *buffer)
{
    if (!devce_card)
        return -ENODEV;

    if ( !(devce_card->ops && devce_card->ops->read) )
        return -EIO;

    struct mmc_card *card = devce_card;
    struct mmc_csd *csd = &(card->csd);
    uint32_t block_size = csd->rd_blk_len;
    uint32_t count_blk = 0;
    uint32_t start_blk = 0;
    int align = 0;
    int ret = 0;

    if (length == 0)
        return 0;

    align = (address & (block_size - 1));
    if (align) {
        printf("%s read address=0x%llx no align(0x%x)\n", __func__, address, block_size);
        return -EINVAL;
    }

    align = (length & (block_size - 1));
    if (align) {
        printf("%s read length=0x%x no align(0x%x)\n", __func__, length, block_size);
        return -EINVAL;
    }

    start_blk = (address + block_size - 1) / block_size;
    count_blk =  (length + block_size - 1) / block_size;
    mutex_lock(&mmc_mutex);
    ret = devce_card->ops->read(card, start_blk, count_blk, buffer);
    mutex_unlock(&mmc_mutex);
    if (ret != count_blk) {
        printf("mmc read failed. start block(%d) read blocks(%d) except blocks(%d)\n", start_blk, ret, count_blk);
        return -1;
    }

    return 0;
}

uint32_t mmc_device_block_write(uint64_t address, uint32_t length, void *buffer)
{
    if (!devce_card)
        return -ENODEV;

    if ( !(devce_card->ops && devce_card->ops->write) )
        return -EIO;

    struct mmc_card *card = devce_card;
    struct mmc_csd *csd = &(card->csd);
    uint32_t count_blk = 0;
    uint32_t start_blk = 0;
    uint32_t block_size = csd->wr_blk_len;
    int align = 0;

    if (length == 0)
        return 0;

    align = (address & (block_size - 1));
    if (align) {
        printf("%s write address=0x%llx no align(0x%x)\n", __func__, address, block_size);
        return -EINVAL;
    }

    align = (length & (block_size - 1));
    if (align) {
        printf("%s write length=0x%x no align(0x%x)\n", __func__, length, block_size);
        return -EINVAL;
    }

    start_blk = (address + block_size - 1) / block_size;
    count_blk = (length + block_size - 1) / block_size;
    mutex_lock(&mmc_mutex);
    int ret = card->ops->write(card, start_blk, count_blk, buffer);
    mutex_unlock(&mmc_mutex);
    if (ret != count_blk) {
        printf("mmc write failed. start block(%d) write blocks(%d) except blocks(%d)\n", start_blk, ret, count_blk);
        return -1;
    }

    return 0;
}

int mmc_device_block_erase(uint64_t address, uint32_t length)
{
    if (!devce_card)
        return -ENODEV;

    if ( !(devce_card->ops && devce_card->ops->erase) )
        return -EIO;

    struct mmc_card *card = devce_card;
    uint32_t erase_size = card->csd.wr_blk_len;
    uint32_t start_blk = 0;
    uint32_t count_blk = 0;
    uint32_t align;

    if (length == 0)
        return 0;

    align = (address & (erase_size - 1));
    if (align) {
        printf("%s erase addr=0x%llx no align(0x%x)\n", __func__, address,erase_size);
        return -EINVAL;
    }

    align = (length & (erase_size - 1));
    if (align) {
        printf("%s erase length=0x%x no align(0x%x)\n", __func__, length, erase_size);
        return -EINVAL;
    }

    start_blk = (address + erase_size - 1) / erase_size;
    count_blk =  (length + erase_size - 1) / erase_size;
    mutex_lock(&mmc_mutex);
    int ret = card->ops->erase(card, start_blk, count_blk);
    mutex_unlock(&mmc_mutex);
    if (ret < 0) {
        printf("mmc erase failed. start block(%d) count (%d)blocks\n", start_blk, count_blk);
        return -1;
    }

    return 0;
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(mmc_devices_info);
EXPORT_SYMBOL(mmc_device_block_read);
EXPORT_SYMBOL(mmc_device_block_write);
EXPORT_SYMBOL(mmc_device_block_erase);

/******************************************************************************
 * 读/写/擦除 测试
 *****************************************************************************/
#define MMC_TEST_WR_RD_ERASE_OFFSET     (0x100000)


static int mmc_blk_test_read(void)
{
#define TEST_CARD_READ_LENGTH           (512)
#define TEST_CARD_READ_OFFSET           MMC_TEST_WR_RD_ERASE_OFFSET

    int ret = 0;
    char *buffer = malloc( TEST_CARD_READ_LENGTH);
    assert(buffer);

    memset(buffer, 0xaa, TEST_CARD_READ_LENGTH);

    ret = mmc_device_block_read(TEST_CARD_READ_OFFSET, TEST_CARD_READ_LENGTH, buffer);
    if (ret < 0) {
        printf("mmc read failed. offset=0x%x length=%d\n", TEST_CARD_READ_OFFSET, TEST_CARD_READ_LENGTH);
        return ret;
    }

    int i = 0;
    uint8_t *tmp = (uint8_t * )(buffer);
    uint32_t read_count = TEST_CARD_READ_LENGTH;

    printf("mmc read start address = 0x%x   size=%d  buffer address=%p\n", TEST_CARD_READ_OFFSET, read_count, tmp);
    for (i = 0; i < read_count; i++) {
        if ( (i != 0) && (i % 16 == 0) )
            printf("\n");

        printf("%02x:", tmp[i]);
    }
    printf("\n");

    free(buffer);

    return ret;
}

static int mmc_blk_test_erase(void)
{
#define TEST_CARD_ERASE_LENGTH          (512)
#define TEST_CARD_ERASE_OFFSET          MMC_TEST_WR_RD_ERASE_OFFSET
    int ret = 0;

    ret = mmc_device_block_erase(TEST_CARD_ERASE_OFFSET, TEST_CARD_ERASE_LENGTH);

    if (ret < 0) {
        printf("mmc erase failed\n");
    } else {
        printf("mmc erase Successfull\n");
    }

    return 0;
}


static int mmc_blk_test_write(void)
{
#define TEST_CARD_WRITE_LENGTH          (512)
#define TEST_CARD_WRITE_OFFSET          MMC_TEST_WR_RD_ERASE_OFFSET

    int ret = 0;
    char *buffer = malloc(TEST_CARD_WRITE_LENGTH);
    int i = 0;
    assert(buffer);

    memset(buffer, 0xaa, TEST_CARD_WRITE_LENGTH);
    char buffer_base = 0x52;
    for (i = 0; i < TEST_CARD_WRITE_LENGTH; i++) {
        buffer[i] = buffer_base;
    }

    printf("mmc write start address = 0x%x   size=%d  buffer address=%p\n", TEST_CARD_WRITE_OFFSET, TEST_CARD_WRITE_LENGTH, buffer);
    ret = mmc_device_block_write(TEST_CARD_WRITE_OFFSET, TEST_CARD_WRITE_LENGTH, buffer);
    if (ret < 0) {
        printf("mmc write failed. offset=0x%x length=%d\n", TEST_CARD_WRITE_OFFSET, TEST_CARD_WRITE_LENGTH);
        return ret;
    }

    free(buffer);
    printf("mmc write Successfull\n");

    return ret;
}

static int mmc_device_block_test(void)
{
    printf("=========mmc_device_block_test==============\n");

    mmc_blk_test_read();

    mmc_blk_test_erase();

    mmc_blk_test_write();

    mmc_blk_test_read();

    return 0;
}
