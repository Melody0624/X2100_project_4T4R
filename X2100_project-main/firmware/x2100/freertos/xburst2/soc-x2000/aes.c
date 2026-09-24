#include <module.h>
#include <errno.h>
#include <os/mutex.h>
#include <os.h>
#include <dfs_file.h>
#include <soc/irq.h>
#include <driver/irq.h>
#include <sched.h>
#include <drivers/usb/hw.h>
#include <driver/clk.h>
#include <assert.h>
#include <string.h>
#include <bit_field.h>
#include "hal/aes_hal.h"
#include <malloc.h>
#include <driver/cache.h>
#include <driver/aes.h>
#define HZ 100

#define MCU_BOOT        0xb3422000
#define DMCS            0xb3421030
#define boot_up_mcu()   *(volatile unsigned int *)(DMCS) = 0;
#define reset_mcu()     *(volatile unsigned int *)(DMCS) = 1;

/* must align with 16 bytes */
#define AES_DMA_LEN     1024
#define AES_ALIGN_LEN   16
#define AES_ALIGN(d,a)  (((d)+((a)-1))/(a)*(a))

struct aes_dev {
    struct aes_config *config;  /* 密钥长度,编解码模式,大小端,密钥,初始化向量 */
    enum aes_dece dece;         /* 编码(0)/解码(1) */
    unsigned int *key;          /* 转化完成密钥存放地址 */
    unsigned int len;           /* 需要加解密的数据长度 */
    unsigned char *src;         /* 需要加解密的数据地址 */
    unsigned char *dst;         /* 加解密完成数据存放地址 */
    unsigned int enable_dma;    /* 默认使用dma模式 */
    unsigned int tc;            /* dma模式下传输次数, 每次传输128位 */
};

struct jz_aes_drv {
    int irq;
    struct clk* clk;
    struct mutex aes_mutex;
    thread_waiter_t dma_wait;
    enum aes_state dma_state;   /* 0: idle 1: busy */
    struct aes_dev dev;
};

static struct jz_aes_drv aes_drv;

static inline void *m_dma_alloc_coherent(int size)
{
    return cache_align_malloc(size);
}

static inline void m_dma_free_coherent(void *mem, int size)
{
    free(mem);
}

static unsigned int aes_set_configs(struct aes_dev *dev)
{
    aes_hal_enable_encrypt();

    aes_hal_mask_all_interrupt();
    aes_hal_clear_all_done_status();

    aes_hal_set_page_size(0x3); /* keep default */

    if (dev->dece != ENCRYPTION && dev->dece != DECRYPTION) {
        printf("AES: failed to get dece! dece: %d\n", dev->dece);
        return -1;
    }

    if (dev->config->mode != ECB_MODE && dev->config->mode != CBC_MODE) {
        printf("AES: failed to get mode(ecb/cbc)! mode: %d\n", dev->config->mode);
        return -1;
    }

    if (dev->config->endian != ENDIAN_LITTLE && dev->config->endian != ENDIAN_BIG) {
        printf("AES: failed to set endian(little/big)! endian: %d\n", dev->config->endian);
        return -1;
    }

    aes_hal_set_data_input_endian(!!dev->config->endian);
    aes_hal_select_dece(dev->dece);
    aes_hal_select_mode(!!dev->config->mode);
    aes_hal_set_key_length(dev->config->keyl & 0x3);

    aes_hal_clear_iv_keys();

    if (dev->enable_dma) {
        aes_hal_enable_dma();
        aes_hal_set_dma_src_addr(dev->src);
        aes_hal_set_dma_dst_addr(dev->dst);
        aes_hal_set_transfer_count(dev->tc);
    }

    return 0;
}

inline unsigned int aes_get_key_bits(enum aes_keyl keyl)
{
    switch (keyl)
    {
    case AES128:
        return 128;

    case AES192:
        return 192;

    case AES256:
        return 256;

    default:
        printf("AES: get key bits failed! keyl: %d\n", keyl);
        break;
    }

    return 0;
}

/* please ensure the key_length should conrresponding to keyl */
int AES_get_key_expansion(unsigned char *ukey, enum aes_keyl keyl, unsigned int *key)
{
    if (ukey == NULL || key == NULL) {
        printf("AES: key_expansion get ukey/key failed!\n");
        return -1;
    }

    if (keyl < AES128 || keyl > AES256) {
        printf("AES: key_expansion get keyl failed!\n");
        return -2;
    }

    int i = 0;
    for (i = 0; i < aes_get_key_bits(keyl) / 8 / 4; i++)
        key[i] = GETU32(ukey + 4 * i);

    return 0;
}

void aes_set_key_expansion(unsigned int *key, enum aes_keyl keyl)
{
    aes_hal_set_keys(key, keyl);

    aes_hal_start_key_expansion();

    while (!aes_hal_get_key_expansion_done_status());

    aes_hal_clear_key_expansion_done_status();
}

unsigned int AES_get_aes_dataout(unsigned char *src, unsigned char *dst)
{
    if (aes_drv.dev.enable_dma) {
        aes_hal_enable_dma();
        aes_hal_start_dma();

        aes_drv.dma_state = aes_BUSY;
        aes_hal_enable_dma_done_interrupt();
        unsigned int timeout = thread_waiter_wait_timeout(&aes_drv.dma_wait, HZ);
        aes_hal_mask_dma_done_interrupt();
        if (timeout) {
            printf("AES: aes encrypt/decrypt dma timeout\n");
            return -1;
        }
        aes_hal_clear_dma_done_status();
        aes_hal_disable_dma();
    } else {
        aes_hal_data_input(src);
        aes_hal_start_encrypt();
        while (!aes_hal_get_aes_done_status());
        aes_hal_clear_aes_done_status();
        aes_hal_data_output(dst);
    }

    return 0;
}

unsigned int AES_transfer_data(struct aes_dev *dev)
{
    clk_enable(aes_drv.clk);
    int ret = aes_set_configs(dev);
    if (ret < 0)
        goto err;

    if (dev->config->mode == CBC_MODE) {
        aes_hal_set_iv(dev->config->iv);
        aes_hal_initial_iv();
    }

    aes_set_key_expansion(dev->key, dev->config->keyl);

    ret = AES_get_aes_dataout(dev->src, dev->dst);

err:
    aes_hal_disable_encrypt();

    clk_disable(aes_drv.clk);

    return ret;
}

/*wake up dma */
static void aes_intr_handler(int irq, void *dev)
{
    unsigned int status = aes_hal_get_all_done_status();
    unsigned int mask = aes_hal_get_all_interrupt();
    aes_hal_clear_all_done_status();
    status = status & mask;

    /* dma_done_status */
    if (status & 0x4) {
        aes_drv.dma_state = aes_IDLE;
        thread_waiter_wakeup(&aes_drv.dma_wait);
    }
}

static int aes_read_data(struct aes_dev *aes_dev)
{
    int ret = 0;

    struct aes_dev *dev = &aes_drv.dev;
    dev->config = aes_dev->config;
    dev->dece = aes_dev->dece;
    dev->len = aes_dev->len;
    unsigned char *ukey = aes_dev->config->ukey;

    unsigned int *key;
    key = (unsigned int *)malloc(sizeof(*key) * 8);
    memset(key, 0, sizeof(*key) * 8);

    unsigned char *src = aes_dev->src;
    unsigned char *dst = aes_dev->dst;

    unsigned int len = dev->len;

    enum aes_keyl keyl = aes_dev->config->keyl;
    ret = AES_get_key_expansion(ukey, keyl, key);
    aes_drv.dev.key = key;

    mutex_lock(&aes_drv.aes_mutex);

    unsigned int N = dev->enable_dma ? AES_DMA_LEN : AES_ALIGN_LEN;

    while (len) {
        int n = len > N ? N : len;
        int size = AES_ALIGN(n, AES_ALIGN_LEN);
        dev->tc = size / AES_ALIGN_LEN;
        memcpy(dev->src, src, n);

        if (size - n)
            memset(&dev->src[n], 0, size - n);

        flush_dcache((unsigned long)dev->src, AES_DMA_LEN);
        invalidate_dcache((unsigned long)dev->dst, AES_DMA_LEN);
        ret = AES_transfer_data(dev);

        if (ret < 0) {
            printf("AES: AES_transfer_data failed\n");
            break;
        }

        if (dev->config->mode == CBC_MODE) {
            if (dev->dece == ENCRYPTION)
                memcpy(dev->config->iv, &dev->dst[size-AES_ALIGN_LEN], AES_ALIGN_LEN);
            else if (dev->dece == DECRYPTION)
                memcpy(dev->config->iv, &dev->src[size-AES_ALIGN_LEN], AES_ALIGN_LEN);
        }

        memcpy(dst, dev->dst, len);
        len -= n;
        src += n;
        dst += size;
    }

    mutex_unlock(&aes_drv.aes_mutex);

    return ret;
}

int soc_aes_encryption(struct aes_config *config, void *src, void *dst, int len)
{
    struct aes_dev aes_dev;
    aes_dev.config = config;
    aes_dev.src = src;
    aes_dev.dst = dst;
    aes_dev.len = len;
    aes_dev.dece = ENCRYPTION;
    int ret = aes_read_data(&aes_dev);
    return ret;
}

int soc_aes_decryption(struct aes_config *config, void *src, void *dst, int len)
{
    struct aes_dev aes_dev;
    aes_dev.config = config;
    aes_dev.src = src;
    aes_dev.dst = dst;
    aes_dev.len = len;
    aes_dev.dece = DECRYPTION;
    int ret = aes_read_data(&aes_dev);
    return ret;
}

static noinline void pdma_wait(void)
{
    __asm__ volatile (
        "    .set    push        \n\t"
        "    .set    noreorder    \n\t"
        "    .set    mips32        \n\t"
        "    li    $26, 0        \n\t"
        "    mtc0    $26, $12    \n\t"
        "    nop            \n\t"
        "1:                \n\t"
        "    wait            \n\t"
        "    b    1b        \n\t"
        "    nop            \n\t"
        "    .set    reorder        \n\t"
        "    .set    pop        \n\t"
        );
}

int soc_aes_init(void)
{
    struct jz_aes_drv *drv = &aes_drv;
    memset(drv, 0, sizeof(struct jz_aes_drv));
    memcpy(&drv->dev, &aes_drv.dev, sizeof(struct aes_dev));

    os_enter_critical();

    clk_enable(clk_get("gate_pdma"));
    clk_enable(clk_get("gate_intc"));
    drv->clk = clk_get("gate_aes");
    clk_enable(drv->clk);

    reset_mcu();
    memcpy((void*)MCU_BOOT, pdma_wait, 64);
    boot_up_mcu();

    aes_hal_disable_encrypt();
    aes_hal_mask_all_interrupt();
    aes_hal_clear_all_done_status();

    thread_waiter_init(&drv->dma_wait);
    mutex_init(&drv->aes_mutex);

    mutex_lock(&aes_drv.aes_mutex);
    os_exit_critical();

    aes_hal_enable_encrypt();

    drv->irq = IRQ_AES;
    request_irq(drv->irq, IRQ_TYPE_NONE, aes_intr_handler, "aes", NULL);

    mutex_unlock(&aes_drv.aes_mutex);

    /* changing enable_dma can control whether dma mode(1) or normal mode(0) is used */
    drv->dev.enable_dma = 1;
    drv->dev.src = m_dma_alloc_coherent(AES_DMA_LEN);
    drv->dev.dst = m_dma_alloc_coherent(AES_DMA_LEN);

    if (drv->dev.src == NULL || drv->dev.dst == NULL)
        return -ENOMEM;

    flush_dcache((unsigned long)drv->dev.src, AES_DMA_LEN);
    invalidate_dcache((unsigned long)drv->dev.dst, AES_DMA_LEN);

    return 0;
}

void soc_aes_exit(void)
{
    os_enter_critical();
    struct jz_aes_drv *drv = &aes_drv;

    disable_irq(IRQ_AES);
    release_irq(IRQ_AES);

    aes_hal_disable_encrypt();

    clk_disable(drv->clk);

    m_dma_free_coherent(drv->dev.src, AES_DMA_LEN);
    m_dma_free_coherent(drv->dev.dst, AES_DMA_LEN);
    os_exit_critical();
}
