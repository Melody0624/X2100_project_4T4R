#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <common.h>
#include <os.h>
#include <driver/cache.h>
#include <driver/gpio.h>
// #undef CONFIG_XBURST2

struct compress_rtos_header {
    unsigned int code[2];
    unsigned int tag;
    unsigned int version;
    unsigned long img_start;
    unsigned long img_end;
    unsigned long heap_start;
    unsigned long heap_end;
    unsigned long mapped_rtosdata_size;
    unsigned long compress_size;
};

static thread_waiter_t wait_load_rtos;

#ifdef CONFIG_XBURST2

#define UNCOMPRESS_ALLOC_MAX_SIZE 0x100000
static unsigned char uncompress_alloc_mem[UNCOMPRESS_ALLOC_MAX_SIZE];
static unsigned long uncompress_alloc_p = 0;

static void *uncompress_alloc(size_t size)
{
    if (uncompress_alloc_p + size >= UNCOMPRESS_ALLOC_MAX_SIZE)
        panic("lzma alloc over!\n");

    void *p = &uncompress_alloc_mem[uncompress_alloc_p];
    uncompress_alloc_p += size;
    return p;
}

static void uncompress_free(void)
{
    uncompress_alloc_p = 0;
}

volatile static int cpu1_uncompres_finish = 0;

#define wait_cond(valid)    do { \
    if (valid >= 1) { \
        break; \
    } \
} while(1); \

#endif

#include <driver/irq.h>
#include <cpu/cpu.h>
#include <zlib/zlib.h>
#include <be_byteshift.h>
#include <driver/sfc_nand.h>
#include <driver/sfc_nor.h>
#include <driver/mmc_device.h>

#define GZIP_HEADER_SIZE        4
#define GZIP_HEADER_NUN         16
#define GZIP_HEADER_SUM_SIZE    (GZIP_HEADER_SIZE * GZIP_HEADER_NUN * 2)

#define COMPRE_IMG_HEADER_SIZE   (sizeof(struct compress_rtos_header))

#define OTA_PARTITION_NAME      "ota"
#define RTOS_PARTITION_NAME     "rtos1"
#define OTARTOS_PARTITION_NAME  "rtos2"

static inline void *is_address_valid(unsigned long address)
{
    if (address > CKSEG0 && address < CKSEG2)
        return (void *)address;
    else
        return NULL;
}

static thread_waiter_t uncompress_wait_load;

unsigned long load_size[GZIP_HEADER_NUN * 2];
unsigned char *load_addr[GZIP_HEADER_NUN] = {NULL, };
unsigned long image_offset;
static void *image_dst_start;
unsigned long image_dst[GZIP_HEADER_NUN];

#ifdef CONFIG_ZLIB
#include <zlib/src/gunzip.h>

#ifdef CONFIG_XBURST2

void *zlib_malloc(void *x, unsigned items, unsigned size)
{
    void *p;

    size *= items;
    size = (size + ZALLOC_ALIGNMENT - 1) & ~(ZALLOC_ALIGNMENT - 1);

    p = uncompress_alloc(size);

    return (p);
}

void zlib_free(void *x, void *addr)
{
    uncompress_free();
}

static void uimage_uncompress_thread_withcpu1(void)
{
    int cur_count = 0;
    unsigned char *dst = (unsigned char *)image_dst_start;

    do {
        if (load_addr[cur_count] == NULL) {
            mdelay(1);
            continue;
        }

        if (gunzip(dst, image_dst[cur_count], load_addr[cur_count], &load_size[cur_count], zlib_malloc, zlib_free) != 0) {
            break;
        }

        dst += (image_dst[cur_count] + image_dst[cur_count + 1]);
        cur_count += 2;

    } while(load_size[cur_count] != 0) ;

    cpu1_uncompres_finish = 1;
    arch_shutdown_current_cpu();
}
#endif

static void uimage_uncompress_thread(void *data)
{
    unsigned char *dst = (unsigned char *)image_dst_start;
    int cur_count = 1;

    thread_waiter_wait(&uncompress_wait_load);
#ifdef CONFIG_XBURST2
    dst += image_dst[0];
#else
    cur_count = 0;
#endif

    while(load_size[cur_count] != 0) {
        if (load_addr[cur_count] == NULL) {
            thread_waiter_wait(&uncompress_wait_load);
            if (load_addr[cur_count] == NULL)
                continue;
        }

        if (gunzip(dst, image_dst[cur_count], load_addr[cur_count], &load_size[cur_count], NULL, NULL) != 0) {
            printf("GUNZIP: uncompress, out-of-mem or overwrite "
                "error - must RESET board to recover\n");
        }
 #ifdef CONFIG_XBURST2
        dst += (image_dst[cur_count] + image_dst[cur_count + 1]);
        cur_count += 2;
#else
        dst += image_dst[cur_count];
        cur_count++;
#endif
    }

    thread_waiter_wakeup(&wait_load_rtos);
}
#elif defined(CONFIG_LZMA)
#include "lzma/src/LzmaLib.h"
#include "lzma/src/LzmaDec.h"

#ifdef CONFIG_XBURST2

static void *lzma_alloc(ISzAllocPtr _p, size_t size)
{
    return uncompress_alloc(size);
}

static void lzma_free(ISzAllocPtr _p, void *addr)
{
    uncompress_free();
}

static void uimage_uncompress_thread_withcpu1(void)
{
    int cur_count = 0;
    unsigned long input_size = 0;
    unsigned long output_size = 0;
    unsigned char *image_addr;
    unsigned char *dst = (unsigned char *)image_dst_start;
    ELzmaStatus status;
    int ret;
    struct ISzAlloc iszalloc = {lzma_alloc, lzma_free};

    do {
        if (load_addr[cur_count] == NULL) {
            mdelay(1);
            continue;
        }

        image_addr = load_addr[cur_count];
        output_size = image_dst[cur_count];

        input_size = load_size[cur_count] - 13;
        ret = LzmaDecode(dst, &output_size, image_addr + 13, &input_size, image_addr, 5,\
                 LZMA_FINISH_ANY, &status, &iszalloc);
        if (ret != 0)
            break;

        dst += (image_dst[cur_count] + image_dst[cur_count + 1]);
        cur_count += 2;
    } while(load_size[cur_count] != 0) ;

    cpu1_uncompres_finish = 1;
    arch_shutdown_current_cpu();
}
#endif

static void uimage_uncompress_thread(void *data)
{
    int cur_count = 1;
    unsigned long input_size = 0;
    unsigned long output_size = 0;
    unsigned char *image_addr;
    unsigned char *dst = (unsigned char *)image_dst_start;
    int ret;

    thread_waiter_wait(&uncompress_wait_load);
#ifdef CONFIG_XBURST2
    dst += image_dst[0];
#else
    cur_count = 0;
#endif

    while(load_size[cur_count] != 0) {
        if (load_addr[cur_count] == NULL) {
            thread_waiter_wait(&uncompress_wait_load);
            if (load_addr[cur_count] == NULL) {
                continue;
            }
        }
        output_size = image_dst[cur_count];
        image_addr = load_addr[cur_count];

        input_size = load_size[cur_count] - 13;

        ret = LzmaUncompress(dst, &output_size, image_addr + 13, &input_size, image_addr, 5);
        if (ret != 0) {
            printf("lzma: uncompress error, ret = %d\n", ret);
            break;
        }
#ifdef CONFIG_XBURST2
        dst += (image_dst[cur_count] + image_dst[cur_count + 1]);
        cur_count += 2;
#else
        dst += image_dst[cur_count];
        cur_count++;
#endif
    }
    thread_waiter_wakeup(&wait_load_rtos);
}
#else
#error RTOS not select third party(glib or lzma)!;
#endif

#ifdef CONFIG_EMMC_DEVICE

#define MMC_SECTOR_SIZE                 (512)

uint32_t mmc_device_block_read(uint64_t address, uint32_t length, void *buffer);
//emmc 存储介质待验证

static int mmc_load_partition(char *partition, char **mem)
{
    int ret;
    uint64_t offset, size;
    char *buf;

    ret = get_mmc_partition_information_by_name(partition, &offset, &size);
    if (ret < 0) {
        printf("not %s partition\n", partition);
        return ret;
    }

    buf = memalign(4096, size);
    if (!buf) {
        printf("malloc buf size %d fail\n", size);
        return -1;
    }

    ret = mmc_device_block_read(offset, size, (uint8_t *)buf);
    if (ret != size) {
        printf("load %s partition fail!\n", partition);
        return -1;
    }

    *mem = buf;
    return 0;
}

static struct compress_rtos_header *mmc_load_rtos_header(char *partition_name)
{
    int ret;
    uint64_t size;
    uint64_t offset;
    struct compress_rtos_header *header = NULL;
    int size_sector = (COMPRE_IMG_HEADER_SIZE + MMC_SECTOR_SIZE - 1) / MMC_SECTOR_SIZE;

    ret = get_mmc_partition_information_by_name(partition_name, &offset, &size);
    if (ret < 0) {
        printf("get %s partition information fail!\n", partition_name);
        return NULL;
    }

    size = size_sector * MMC_SECTOR_SIZE;
    header = memalign(4096, size);
    if (!header) {
        printf("malloc header size %d fail\n", size);
        return NULL;
    }

    ret = mmc_device_block_read(offset, size, (void *)header);
    if (ret != size) {
        printf("load %s partition fail!\n", partition_name);
        return NULL;
    }

    image_offset = offset;

    return header;
}

static int mmc_load_rtos(struct compress_rtos_header *header)
{
    unsigned long offset = image_offset + COMPRE_IMG_HEADER_SIZE;
    unsigned long uimage_size = get_unaligned_be32(&header->compress_size) - GZIP_HEADER_SUM_SIZE;
    unsigned long load_size_sum = 0;
    int size_sector = (GZIP_HEADER_SUM_SIZE + MMC_SECTOR_SIZE - 1) / MMC_SECTOR_SIZE;
    int size = size_sector * MMC_SECTOR_SIZE;
    int ret;
    int i;

    void *rtos_load_image_addr = cache_align_malloc(uimage_size);
    if (!rtos_load_image_addr) {
        printf("malloc uimage_size %ld fail\n", uimage_size);
        return -1;
    }

    ret = mmc_device_block_read(offset, size, (void *)load_size);
    if (ret != size) {
        printf("RTOS mmc load rtos failed.\n");
        return ret;
    }

    for (i=0; i<GZIP_HEADER_NUN; i++) {
        load_size[i] = get_unaligned_be32(&load_size[i]);
        load_size_sum += load_size[i];
    }
    for (; i<GZIP_HEADER_NUN * 2; i++) {
        image_dst[i - GZIP_HEADER_NUN] = get_unaligned_be32(&load_size[i]);
    }
    offset += GZIP_HEADER_SUM_SIZE;

    if (uimage_size != load_size_sum) {
        printf("The loaded size is not equal to the uImage size(load size = %d, uImage size = %d)\n", load_size_sum, uimage_size);
        return -1;
    }

    for(int i=0; load_size[i] != 0; i++) {
        size_sector = (load_size[i] + MMC_SECTOR_SIZE - 1) / MMC_SECTOR_SIZE;
        size = size_sector * MMC_SECTOR_SIZE;
        ret = mmc_device_block_read(offset, size, (void *)rtos_load_image_addr);
        if (ret != size) {
            printf("RTOS mmc load rtos failed.\n");
            return ret;
        }

        load_addr[i] = rtos_load_image_addr;
#ifdef CONFIG_XBURST2
        if (i == 0)
            arch_startup_cpu(1, (unsigned long)uimage_uncompress_thread_withcpu1);
        if (i%2 != 0)
#endif
            thread_waiter_wakeup(&uncompress_wait_load);

        rtos_load_image_addr += load_size[i];
        offset += load_size[i];
    }

#ifdef CONFIG_XBURST2
    if (load_size_sum == load_size[0])
        thread_waiter_wakeup(&uncompress_wait_load);
#endif
    return ret;
}
#endif

#ifdef CONFIG_SFC_NAND
#include <driver/sfc_nand.h>

static int nand_load_partition(char *partition, char **mem)
{
    int ret;
    unsigned int offset, size;
    char *buf;

    ret = get_nand_partition_information_by_name(partition, &offset, &size);
    if (ret < 0) {
        printf("not %s partition\n", partition);
        return ret;
    }

    buf = memalign(4096, size);
    if (!buf) {
        printf("malloc buf size %d fail\n", size);
        return -1;
    }

    ret = sfc_nand_flash_read(offset, size, (uint8_t *)buf);
    if (ret != size) {
        printf("load %s partition fail!\n", partition);
        return -1;
    }

    *mem = buf;
    return 0;
}

static struct compress_rtos_header *nand_load_rtos_header(char *partition_name)
{
    int ret;
    unsigned int offset, size;
    struct compress_rtos_header *header = NULL;

    ret = get_nand_partition_information_by_name(partition_name, &offset, &size);
    if (ret < 0) {
        printf("get %s partition information fail!\n", partition_name);
        return NULL;
    }

    size = ALIGN(sizeof(struct compress_rtos_header), cache_line_size());

    header = memalign(4096, size);
    if (!header) {
        printf("malloc header size %d fail\n", size);
        return NULL;
    }

    ret = sfc_nand_flash_read(offset, size, (uint8_t *)header);
    if (ret != size) {
        printf("load %s partition fail!\n", partition_name);
        return NULL;
    }

    image_offset = offset;

    return header;
}

static int nand_load_rtos(struct compress_rtos_header *header)
{
    unsigned long offset = image_offset + COMPRE_IMG_HEADER_SIZE;
    unsigned long uimage_size = get_unaligned_be32(&header->compress_size) - GZIP_HEADER_SUM_SIZE;
    unsigned long load_size_sum = 0;
    int ret;
    int i;

    void *rtos_load_image_addr = cache_align_malloc(uimage_size);
    if (!rtos_load_image_addr) {
        printf("malloc uimage_size %ld fail\n", uimage_size);
        return -1;
    }

    ret = sfc_nand_flash_read(offset, GZIP_HEADER_SUM_SIZE, (uint8_t *)load_size);
    if (ret != GZIP_HEADER_SUM_SIZE) {
        printf("RTOS nand load rtos: total write length(%d) not equal actual(%d).\n", GZIP_HEADER_SUM_SIZE, ret);
        return ret;
    }

    for (i=0; i<GZIP_HEADER_NUN; i++) {
        load_size[i] = get_unaligned_be32(&load_size[i]);
        load_size_sum += load_size[i];
    }
    for (; i<GZIP_HEADER_NUN * 2; i++) {
        image_dst[i - GZIP_HEADER_NUN] = get_unaligned_be32(&load_size[i]);
    }
    offset += GZIP_HEADER_SUM_SIZE;

    if (uimage_size != load_size_sum) {
        printf("The loaded size is not equal to the uImage size(load size = %ld, uImage size = %ld)\n", load_size_sum, uimage_size);
        return -1;
    }

    for(i=0; load_size[i] != 0; i++) {
        ret = sfc_nand_flash_read(offset, load_size[i], (uint8_t *)rtos_load_image_addr);
        if (ret != load_size[i]) {
            printf("RTOS nand load rtos: total write length(%ld) not equal actual(%d).\n", load_size[i], ret);
            return ret;
        }

        load_addr[i] = rtos_load_image_addr;
#ifdef CONFIG_XBURST2
        if (i == 0)
            arch_startup_cpu(1, (unsigned long)uimage_uncompress_thread_withcpu1);
        if (i%2 != 0)
#endif
            thread_waiter_wakeup(&uncompress_wait_load);

        rtos_load_image_addr += load_size[i];
        offset += load_size[i];
    }

#ifdef CONFIG_XBURST2
    if (load_size_sum == load_size[0])
        thread_waiter_wakeup(&uncompress_wait_load);
#endif
    return ret;
}
#endif

#ifdef CONFIG_SFC_NOR
#include <driver/sfc_nor.h>

static int nor_load_partition(char *partition, char **mem)
{
    int ret;
    unsigned int offset, size;
    char *buf;

    ret = get_nor_partition_information_by_name(partition, &offset, &size);
    if (ret < 0) {
        printf("not %s partition\n", partition);
        return ret;
    }

    buf = memalign(4096, size);
    if (!buf) {
        printf("malloc buf size %d fail\n", size);
        return -1;
    }

    ret = sfc_nor_flash_read(offset, size, (uint8_t *)buf);
    if (ret != size) {
        printf("load %s partition fail!\n", partition);
        return -1;
    }

    *mem = buf;
    return 0;
}

static struct compress_rtos_header *nor_load_rtos_header(char *partition_name)
{
    int ret;
    unsigned int offset, size;
    struct compress_rtos_header *header = NULL;

    ret = get_nor_partition_information_by_name(partition_name, &offset, &size);
    if (ret < 0) {
        printf("get %s partition information fail!\n", partition_name);
        return NULL;
    }

    size = ALIGN(sizeof(struct compress_rtos_header), cache_line_size());
    header = memalign(4096, size);
    if (!header) {
        printf("malloc header size %d fail\n", size);
        return NULL;
    }

    ret = sfc_nor_flash_read(offset, size, (uint8_t *)header);
    if (ret != size) {
        printf("load %s partition fail!\n", partition_name);
        return NULL;
    }

    image_offset = offset;

    return header;
}

static int nor_load_rtos(struct compress_rtos_header *header)
{
    unsigned long offset = image_offset + COMPRE_IMG_HEADER_SIZE;
    unsigned long uimage_size = get_unaligned_be32(&header->compress_size) - GZIP_HEADER_SUM_SIZE;
    unsigned long load_size_sum = 0;
    int ret;
    int i;

    void *rtos_load_image_addr = cache_align_malloc(uimage_size);
    if (!rtos_load_image_addr) {
        printf("malloc uimage_size %ld fail\n", uimage_size);
        return -1;
    }

    ret = sfc_nor_flash_read(offset, GZIP_HEADER_SUM_SIZE, (uint8_t *)load_size);
    if (ret != GZIP_HEADER_SUM_SIZE) {
        printf("RTOS nor load rtos: total write length(%d) not equal actual(%d).\n", GZIP_HEADER_SUM_SIZE, ret);
        return ret;
    }

    for (i=0; i<GZIP_HEADER_NUN; i++) {
        load_size[i] = get_unaligned_be32(&load_size[i]);
        load_size_sum += load_size[i];
    }
    for (; i<GZIP_HEADER_NUN * 2; i++) {
        image_dst[i - GZIP_HEADER_NUN] = get_unaligned_be32(&load_size[i]);
    }

    if (uimage_size != load_size_sum) {
        printf("The loaded size is not equal to the uImage size(load size = %ld, uImage size = %ld)\n", load_size_sum, uimage_size);
        return -1;
    }

    offset += GZIP_HEADER_SUM_SIZE;
    for(i=0; load_size[i] != 0; i++) {
        ret = sfc_nor_flash_read(offset, load_size[i], (uint8_t *)rtos_load_image_addr);
        if (ret != load_size[i]) {
            printf("RTOS nor load rtos: total write length(%ld) not equal actual(%d).\n", load_size[i], ret);
            return ret;
        }

        load_addr[i] = (unsigned char *)rtos_load_image_addr;
#ifdef CONFIG_XBURST2
        if (i == 0)
            arch_startup_cpu(1, (unsigned long)uimage_uncompress_thread_withcpu1);
        if (i%2 != 0)
#endif
            thread_waiter_wakeup(&uncompress_wait_load);

        rtos_load_image_addr += load_size[i];
        offset += load_size[i];
    }

#ifdef CONFIG_XBURST2
    if (load_size_sum == load_size[0])
        thread_waiter_wakeup(&uncompress_wait_load);
#endif
    return ret;
}
#endif

static int load_rtos_partition(struct compress_rtos_header *header)
{
#ifdef CONFIG_EMMC_DEVICE
    return mmc_load_rtos(header);
#elif defined(CONFIG_SFC_NAND)
    return nand_load_rtos(header);
#elif defined(CONFIG_SFC_NOR)
    return nor_load_rtos(header);
#else
    printf("RTOS: can't load rtos image, only support emmc_device & sfc_nand & sfc_nor!\n");
    return -EACCES;
#endif
}

static void load_rtos(void *data)
{
    int ret;
    struct compress_rtos_header *os_args = (struct compress_rtos_header *)data;

    ret = load_rtos_partition(os_args);
    if (ret < 0)
        panic("RTOS: load rtos failed!!!\n");
}


static struct compress_rtos_header *load_rtos_header(char *partition_name)
{
#ifdef CONFIG_EMMC_DEVICE
    return mmc_load_rtos_header(partition_name);
#elif defined(CONFIG_SFC_NAND)
    return nand_load_rtos_header(partition_name);
#elif defined(CONFIG_SFC_NOR)
    return nor_load_rtos_header(partition_name);
#else
    printf("RTOS: can't load rtos header, only support emmc_device & sfc_nand & sfc_nor!\n");
    return -EACCES;
#endif
}

static char *get_rtos_partition_name(void)
{
    char *ota_text = NULL;
    char *rtos2 = "ota:"OTARTOS_PARTITION_NAME;
    int ret;

#ifdef CONFIG_EMMC_DEVICE
    ret = mmc_load_partition(OTA_PARTITION_NAME, &ota_text);
#elif defined(CONFIG_SFC_NAND)
    ret = nand_load_partition(OTA_PARTITION_NAME, &ota_text);
#elif defined(CONFIG_SFC_NOR)
    ret = nor_load_partition(OTA_PARTITION_NAME, &ota_text);
#else
    printf("RTOS: can't load ota partition, only support emmc_device & sfc_nand & sfc_nor!\n");
    return -EACCES;
#endif

    if (ret)
        return RTOS_PARTITION_NAME;

    if (!strncmp(rtos2, ota_text, strlen(rtos2))) {
        return OTARTOS_PARTITION_NAME;
    }

    return RTOS_PARTITION_NAME;
}

static void load_rtos_split(void)
{
    char *rtos_name = get_rtos_partition_name();

    printf("now uncompress : %s\n", rtos_name);

    struct compress_rtos_header *header = load_rtos_header(rtos_name);

    thread_waiter_init(&wait_load_rtos);
    thread_waiter_init(&uncompress_wait_load);
    image_dst_start = (void *)header->img_start;


    // 开启线程
    thread_create("load uImage", 8192, load_rtos, header);
    thread_create("uncompress uImage", 8192, uimage_uncompress_thread, header);

    thread_waiter_wait(&wait_load_rtos);
#ifdef CONFIG_XBURST2
    wait_cond(cpu1_uncompres_finish);
#endif

    printf("ready jump to: %s\n", rtos_name);

    flush_cache_all();
    release_all_irq();

	typedef void (*image_entry_arg_t)(void)
		__attribute__ ((noreturn));
	image_entry_arg_t image_entry =
		(image_entry_arg_t) header->img_start;

    image_entry();
}
