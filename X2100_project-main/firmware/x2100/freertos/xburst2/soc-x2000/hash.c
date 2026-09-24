#include <common.h>
#include <soc/base.h>
#include <driver/cache.h>
#include <driver/clk.h>
#include <driver/gpio.h>
#include <driver/irq.h>
#include <os.h>
#include <wake_lock.h>
#include <stdio.h>
#include <assert.h>
#include <bit_field.h>
#include <soc/hash.h>
#include "hash_regs.h"

#define HASH_IOBASE      0x13470000
#define HASH_ADDR(reg)   ((volatile unsigned long *)((KSEG1ADDR(HASH_IOBASE)) + (reg)))

#define HASH_START       1
#define HASH_END         0

struct hash_device {
    thread_waiter_t wait;
    struct clk* clk;
    unsigned int *mem;
    struct mutex work;
} jz_hash;
struct data_attr {
    enum encryption_mode mode;
    struct mutex ops;
    unsigned int total_input_size;
    unsigned int output_size;
    unsigned int buffer_size;
    unsigned char buffer[64];
} hash_data;



static inline void jz_hash_write_reg(unsigned int reg, unsigned int value)
{
    *HASH_ADDR(reg) = value;
}

static inline unsigned int jz_hash_read_reg(unsigned int reg)
{
    return *HASH_ADDR(reg);
}

static inline void jz_hash_set_bit(unsigned int reg, int start, int end, unsigned int value)
{
    set_bit_field_v(HASH_ADDR(reg), start, end, value);
}

static inline unsigned int jz_hash_get_bit(unsigned int reg, int start, int end)
{
    return get_bit_field_v(HASH_ADDR(reg), start, end);
}

static inline void *m_dma_alloc_coherent(int dma_size)
{
    return cache_align_malloc(dma_size);
}

static inline void m_dma_free_coherent(void *mem, int dma_size)
{
    free(mem);
}

static inline void m_cache_sync(void *mem, int dma_size)
{
    flush_dcache_force((unsigned long) mem, dma_size);
}

static void hash_set_dma_addr(void *virt_address)
{
    unsigned int phys_address = virt_to_phys(virt_address);
    jz_hash_write_reg(HSSA, phys_address);
}

static void md5_init_setting(void)
{
    jz_hash_set_bit(HSCR, HSCR_SEL, 0);
    jz_hash_set_bit(HSCR, HSCR_DORVS, 1);
    jz_hash_set_bit(HSCR, HSCR_DIRVS, 0);
    jz_hash_set_bit(HSCR, HSCR_DMAE, 1);
    jz_hash_set_bit(HSCR, HSCR_EN, 1);
}

static void sha1_init_setting(void)
{
    jz_hash_set_bit(HSCR, HSCR_SEL, 1);
    jz_hash_set_bit(HSCR, HSCR_DORVS, 0);
    jz_hash_set_bit(HSCR, HSCR_DIRVS, 1);
    jz_hash_set_bit(HSCR, HSCR_DMAE, 1);
    jz_hash_set_bit(HSCR, HSCR_EN, 1);
}

static void sha224_init_setting(void)
{
    jz_hash_set_bit(HSCR, HSCR_SEL, 2);
    jz_hash_set_bit(HSCR, HSCR_DORVS, 0);
    jz_hash_set_bit(HSCR, HSCR_DIRVS, 1);
    jz_hash_set_bit(HSCR, HSCR_DMAE, 1);
    jz_hash_set_bit(HSCR, HSCR_EN, 1);
}

static void sha256_init_setting(void)
{
    jz_hash_set_bit(HSCR, HSCR_SEL, 3);
    jz_hash_set_bit(HSCR, HSCR_DORVS, 0);
    jz_hash_set_bit(HSCR, HSCR_DIRVS, 1);
    jz_hash_set_bit(HSCR, HSCR_DMAE, 1);
    jz_hash_set_bit(HSCR, HSCR_EN, 1);
}

static unsigned int swap_endian(unsigned int value)
{
    value = ((value << 8) & 0xFF00FF00) | ((value >> 8) & 0xFF00FF);
    return (value << 16) | (value >> 16);
}

static void hash_data_dma_transform(unsigned char *input)
{
    int ret;
    jz_hash_write_reg(HSTC, 1);

    memcpy(jz_hash.mem, input, 64);
    m_cache_sync(jz_hash.mem, 64);

    jz_hash_set_bit(HSINTM, HSINTM_MR_INT_M, 1);
    jz_hash_set_bit(HSCR, HSCR_DMAS, 1);

    ret = thread_waiter_wait_timeout(&jz_hash.wait, 100);
    if (ret == -1)
        panic("hash data transmit timeout\n");

    hash_data.total_input_size += 64;
}

static void hash_data_transform_end(unsigned char *input, unsigned int input_size)
{
    unsigned int bits0, bits1, temp;
    unsigned char dma_array[64];
    unsigned int total_size = hash_data.total_input_size + input_size;

    memcpy(dma_array, input, input_size);
    dma_array[input_size] = 0x80;
    input_size = input_size + 1;

    memset(dma_array+input_size, 0, 64-input_size);
    if (input_size > 56) {
        hash_data_dma_transform(dma_array);
        memset(dma_array, 0, 64);
    }

    bits0 = total_size << 3;
    bits1 = total_size >> 29;
    if (hash_data.mode != MD5) {
        bits0 = swap_endian(bits0);
        bits1 = swap_endian(bits1);
        temp = bits0;
        bits0 = bits1;
        bits1 = temp;
    }
    memcpy(dma_array+56, &bits0, 4);
    memcpy(dma_array+60, &bits1, 4);
    hash_data_dma_transform(dma_array);
}

static void hash_irq_handler(int irq, void *data)
{
    jz_hash_set_bit(HSSR, HSSR_MRD, 1);
    thread_waiter_wakeup(&jz_hash.wait);
}

static void jz_hash_encryption_deinit(void)
{
    jz_hash_set_bit(HSCR, HSCR_DMAE, 0);
    jz_hash_set_bit(HSCR, HSCR_EN, 0);
}

static int jz_hash_encryption_init(enum encryption_mode mode)
{
    switch(mode) {
        case MD5:
            md5_init_setting();
            hash_data.output_size = 16;
            break;
        case SHA1:
            sha1_init_setting();
            hash_data.output_size = 20;
            break;
        case SHA224:
            sha224_init_setting();
            hash_data.output_size = 28;
            break;
        case SHA256:
            sha256_init_setting();
            hash_data.output_size = 32;
            break;
        default:
            printf("not support this mode\n");
            jz_hash_encryption_deinit();
            return -1;
    }

    return 0;
}

static void jz_hash_encryption_write(unsigned char *input, unsigned int input_size)
{
    int remain_size;
    if (hash_data.buffer_size) {
        remain_size = 64 - hash_data.buffer_size;
        if (input_size >= remain_size) {
            memcpy(hash_data.buffer + hash_data.buffer_size, input, remain_size);
            hash_data.buffer_size = 0;
            hash_data_dma_transform(hash_data.buffer);
            input += remain_size;
            input_size -= remain_size;
        }
    }

    while (input_size >= 64) {
        hash_data_dma_transform(input);
        input += 64;
        input_size -= 64;
    }

    if (input_size) {
        memcpy(hash_data.buffer + hash_data.buffer_size, input, input_size);
        hash_data.buffer_size += input_size;
    }
}

static void jz_hash_encryption_read(unsigned int *result)
{
    int i;
    hash_data_transform_end(hash_data.buffer, hash_data.buffer_size);
    hash_data.buffer_size = 0;
    jz_hash_set_bit(HSCR, HSCR_INIT, 1);

    for (i = 0; i < hash_data.output_size / 4; i++) {
        result[i] = jz_hash_read_reg(HSDO);
        result[i] = swap_endian(result[i]);
    }
}

void soc_hash_init(void)
{
    mutex_init(&jz_hash.work);
    thread_waiter_init(&jz_hash.wait);

    jz_hash.clk = clk_get("gate_hash");
    assert(jz_hash.clk);
    clk_enable(jz_hash.clk);

    jz_hash.mem = m_dma_alloc_coherent(64);
    assert(jz_hash.mem);
    hash_set_dma_addr(jz_hash.mem);

    request_irq(IRQ_HASH, 0, hash_irq_handler, "hash", NULL);
    enable_irq(IRQ_HASH);
}

int soc_hash_request(enum encryption_mode mode, unsigned int timeout_ms)
{
    int ret;

    ret = mutex_lock_timeout(&jz_hash.work, timeout_ms); //1000是根据wait超时来确定的
    if (ret) {
        printf("ERROR:Hash device is busy!\n");
        return -EBUSY;
    }

    mutex_init(&hash_data.ops);
    hash_data.mode = mode;
    hash_data.total_input_size = 0;
    ret = jz_hash_encryption_init(mode);
    if (ret) {
        mutex_unlock(&jz_hash.work);
        return -1;
    }

    return (int)&hash_data;
}

int soc_hash_write(int handle, unsigned char *str, unsigned int str_size)
{
    mutex_lock(&hash_data.ops);

    if((int)&hash_data != handle)
        goto unlock;
    if(hash_data.mode == -1)
        goto unlock;
    jz_hash_encryption_write(str, str_size);

    mutex_unlock(&hash_data.ops);
    return 0;

unlock:
    mutex_unlock(&hash_data.ops);
    return -1;
}

int soc_hash_read_free(int handle, unsigned char *rec, unsigned long hash_size)
{
    mutex_lock(&hash_data.ops);

    if((int)&hash_data != handle || rec == NULL)
        goto unlock;
    if (hash_size != hash_data.output_size) {
        printf("ERROR:Hash encryption accept size error\n");
        goto unlock;
    }

    jz_hash_encryption_read((void *)rec);
    jz_hash_encryption_deinit();
    hash_data.mode = -1;

    mutex_unlock(&hash_data.ops);

    mutex_unlock(&jz_hash.work);
    return 0;

unlock:
    mutex_unlock(&hash_data.ops);
    return -1;
}

void soc_hash_deinit(void)
{
    disable_irq(IRQ_HASH);
    release_irq(IRQ_HASH);

    m_dma_free_coherent(jz_hash.mem, 64);

    clk_disable(jz_hash.clk);
    clk_put(jz_hash.clk);
}