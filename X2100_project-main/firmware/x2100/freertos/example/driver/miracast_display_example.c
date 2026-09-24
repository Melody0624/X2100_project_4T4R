#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <common.h>
#include <os.h>
#include <driver/cache.h>
#include <driver/gpio.h>
#include <lib/spl_cmdargs_analysis.h>

#ifndef ALIGN_UP
#define ALIGN_UP(x, n)                  (((x) + (n) - 1) - ((x) + (n) - 1) % (n))
#endif

// #undef CONFIG_XBURST2

// #define NOT_DISPLAY /* 不显示logo时开启 */

static thread_waiter_t wait_load_kernel;

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

/******************************release******************************/
#include <driver/irq.h>
#include <cpu/cpu.h>

extern void jump_to_uboot(void);

static void release_all_resources(void)
{
    release_all_irq();

    arch_deinit_cpu();
}

#ifndef NOT_DISPLAY
/******************************boot_logo******************************/
#include <driver/fb.h>
#include <driver/backlight.h>
#include <driver/irq.h>

#include <include_bin.h>
#include <jpegd_decoder.h>

// #define LCD_ROTATE

#ifdef LCD_ROTATE
INCBIN(jpeg_display, "example/resource/ingenic_logo_720p.jpeg");
#else
INCBIN(jpeg_display, "example/resource/ingenic_logo.jpeg");
#endif

static struct fb_info info;
static struct fb_handle *fb0_handle;

static void fb_pan_display_config(struct jpegd_decoder_param *param, struct jpegd_decoder_output *output)
{
    struct lcdc_layer layer_cfg = {
        .fb_fmt = fb_fmt_ARGB8888,
        .xres = output->actual_width, // 实际图像的有效分辨率宽
        .yres = output->actual_height,// 实际图像的有效分辨率高
        .xpos = 0,
        .ypos = 0,

        .layer_order = lcdc_layer_0,
        .layer_enable = 1,

        layer_cfg.rgb.mem = (void *)output->data,
        layer_cfg.rgb.stride = output->width * 4, /* 解码输出width */

        .alpha = {
            .enable = 0,
            .value = 0xff,
        },
    };

    /* 分辨率和屏幕不相等, 缩放填满全屏显示 */
    if (layer_cfg.xres != info.xres || layer_cfg.yres != info.yres) {
        layer_cfg.scaling.enable = 1;
        layer_cfg.scaling.xres = info.xres;//缩放到全屏显示
        layer_cfg.scaling.yres = info.yres;//缩放到全屏显示
    }

    fb_enable_config(fb0_handle);
    fb_set_config(fb0_handle, &layer_cfg);

    fb_pan_display(fb0_handle, 0);
}

/* 初始化fb */
static void enbale_fb_thread(void *data)
{
    fb_enable(fb0_handle);
}

int jpeg_display_to_fb(void *logo_mem, int logo_size)
{
    struct backlight *backlight;
    int ret;

    fb0_handle = fb_open("fb0");
    if(fb0_handle == NULL)
        printf("fb0 = NULL\n");

    fb_get_info(fb0_handle, &info);
    if (info.fb_fmt != fb_fmt_RGB888 && info.fb_fmt != fb_fmt_ARGB8888 ) {
        printf("jpeg_display_to_fb: this just support rgb888! you should change this demo\n");
        return -1;
    }
    thread_create("enbale_fb_thread", 1024, enbale_fb_thread, NULL);

    /* 该 example 默认使用 720x1280 JPEG图像, 若实际图像分辨率大于720x1280, 将解码失败. 请根据实际情况进行修改 */
    struct jpegd_decoder_param param = {
        /* 可以正常解码小于等于 720x1280分辨率的JPEG图像 */
        .width = 720,
        .height = 1280,
        .out_fmt = JPEGD_PIX_FMT_BGRA_8888,
    };

    struct jpegd_decoder *decoder = jpegd_decoder_init(&param);
    if (!decoder) {
        printf("jpeg decoder init failed\n");
        return -1;
    }

    void *vpu_mem_start = NULL;
    unsigned int vpu_mem_size = 0;
    /* 获取vpu内存地址和大小 */
    if (cmdargs_mem_info_get("vpu_mem", &vpu_mem_start, &vpu_mem_size) < 0) {
        printf("err, qucikstart cmdline vpu_mem invalid!");
        goto decode_err;
    }
    printf("cmdline vpu_mem 0x%p sz %u!\n", vpu_mem_start, vpu_mem_size);

    /* miracast 投屏场景, 没有配置fb帧缓冲, 解码出来的用于显示的数据内容拷贝到H264解码预留的内存区域.
       以避免该显示帧数据内容被修改导致的显示异常. 需要根据实际的内存预留情况进行计算.
       并且将解码器预留内存开始地址向上对齐到MB, 作为显示帧存放位置.
    */
    void *h264_reserved_mem = ALIGN_UP((unsigned int)vpu_mem_start, 1024*1024);

    struct jpegd_decoder_output out;
    memset((void *)&out, 0, sizeof(struct jpegd_decoder_output));
    out.data_size = vpu_mem_size - (h264_reserved_mem - vpu_mem_start);
    out.data = h264_reserved_mem;
    printf("decoded image to 0x%p to display!\n", h264_reserved_mem);

    ret = jpegd_decoder_decode(decoder, (void *)logo_mem, logo_size, &out);
    if (ret < 0) {
        printf("jpeg decoder failed! Check the width, height and format of the image used!\n");
        goto decode_err;
    }
    printf("jpegd decoded w %d h %d act w %d act h %d\n", out.width, out.height, out.actual_width, out.actual_height);

    // 等待 fb 开启
    while (!fb_is_enable(fb0_handle)) {
        usleep(100);
    }

    /* 将解码的图像配置为缩放填充满屏显示 */
    fb_pan_display_config(&param, &out);

    // 开启屏幕背光
    backlight = backlight_open("backlight_pwm0");
    if (backlight == NULL) {
        printf("backlight_open fail.\n");
    }

    backlight_set_brightness(backlight, backlight->max_brightness);

decode_err:
    jpegd_decoder_deinit(decoder);
    return ret;
}

static int load_partition_data(uint8_t **mem, char *partition_name);
#define JPEG_HEADER_TAG 0xd8ff
#endif

static void show_logo(void *data)
{
#ifndef NOT_DISPLAY
    uint8_t *logo_mem;
    uint16_t *jpeg_header;
    int logo_size;

    logo_size = load_partition_data(&logo_mem, "logo");
    jpeg_header = (uint16_t *)logo_mem;

    if (logo_size < 0 || *jpeg_header != JPEG_HEADER_TAG) {
        printf("now use default logo\n");
        logo_mem = (uint8_t *)jpeg_displayData;
        logo_size = jpeg_displaySize;
    }

    jpeg_display_to_fb(logo_mem, logo_size);
#endif

    thread_waiter_wait(&wait_load_kernel);
#ifdef CONFIG_XBURST2
    wait_cond(cpu1_uncompres_finish);
#endif

    release_all_resources();
    jump_to_uboot();
}

/******************************load_kernel******************************/

#include <spl_rtos_argument.h>
#include <zlib/zlib.h>
#include <be_byteshift.h>
#include <driver/sfc_nand.h>
#include <driver/sfc_nor.h>
#include <driver/mmc_device.h>

#define GZIP_HEADER_SIZE        4
#define GZIP_HEADER_NUN         16
#define GZIP_HEADER_SUM_SIZE    (GZIP_HEADER_SIZE * GZIP_HEADER_NUN * 2)

#define IMAGE_HEADER_SIZE       64

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

    thread_waiter_wakeup(&wait_load_kernel);
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
    thread_waiter_wakeup(&wait_load_kernel);
}
#else
#error RTOS not select third party(glib or lzma)!;
#endif

#ifdef CONFIG_EMMC_DEVICE

#define MMC_SECTOR_SIZE                 (512)

uint32_t mmc_device_block_read(uint64_t address, uint32_t length, void *buffer);
//emmc 存储介质待验证

static int mmc_load_partition_data(uint8_t **mem, char *partition_name)
{
    int ret;
    uint64_t size;
    uint64_t offset;

    ret = get_mmc_partition_information_by_name(partition_name, &offset, &size);
    if (ret < 0) {
        printf("get %s partition information fail!\n", partition_name);
        return -1;
    }

    *mem = memalign(4096, ALIGN(size, cache_line_size()));
    if (!mem) {
        printf("malloc logo size %d fail\n", size);
        return -1;
    }

    ret = mmc_device_block_read(offset, size, *mem);
    if (ret != size) {
        printf("load %s partition fail!\n", partition_name);
        return -1;
    }

    return size;
}

static int mmc_load_kernel(struct rtos_boot_os_args *args)
{
    unsigned long offset = args->offset + IMAGE_HEADER_SIZE;
    unsigned long uimage_size = args->size - IMAGE_HEADER_SIZE - GZIP_HEADER_SUM_SIZE;
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
        printf("RTOS mmc load kernel failed.\n");
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
            printf("RTOS mmc load kernel failed.\n");
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

static int nand_load_partition_data(uint8_t **mem, char *partition_name)
{
    int ret;
    unsigned int offset, size;

    ret = get_nand_partition_information_by_name(partition_name, &offset, &size);
    if (ret < 0) {
        printf("get %s partition information fail!\n", partition_name);
        return -1;
    }

    *mem = memalign(4096, ALIGN(size, cache_line_size()));
    if (!mem) {
        printf("malloc logo size %d fail\n", size);
        return -1;
    }

    ret = sfc_nand_flash_read(offset, size, *mem);
    if (ret != size) {
        printf("load %s partition fail!\n", partition_name);
        return -1;
    }

    return size;
}

static int nand_load_kernel(struct rtos_boot_os_args *args)
{
    unsigned int offset = args->offset + IMAGE_HEADER_SIZE;
    unsigned long uimage_size = args->size - IMAGE_HEADER_SIZE - GZIP_HEADER_SUM_SIZE;
    unsigned long load_size_sum = 0;
    int ret;
    int i;

    void *rtos_load_image_addr = cache_align_malloc(uimage_size);
    if (!rtos_load_image_addr) {
        printf("malloc uimage_size %ld fail\n", uimage_size);
        return -1;
    }

    ret = sfc_nand_flash_read_check_badblock(&offset, GZIP_HEADER_SUM_SIZE, (uint8_t *)load_size);
    if (ret != GZIP_HEADER_SUM_SIZE) {
        printf("RTOS nand load kernel: total write length(%d) not equal actual(%d).\n", GZIP_HEADER_SUM_SIZE, ret);
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
        ret = sfc_nand_flash_read_check_badblock(&offset, load_size[i], (uint8_t *)rtos_load_image_addr);
        if (ret != load_size[i]) {
            printf("RTOS nand load kernel: total write length(%ld) not equal actual(%d).\n", load_size[i], ret);
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

static int nor_load_partition_data(uint8_t **mem, char *partition_name)
{
    int ret;
    unsigned int offset, size;

    ret = get_nor_partition_information_by_name(partition_name, &offset, &size);
    if (ret < 0) {
        printf("get %s partition information fail!\n", partition_name);
        return -1;
    }

    *mem = memalign(4096, ALIGN(size, cache_line_size()));
    if (!mem) {
        printf("malloc logo size %d fail\n", size);
        return -1;
    }

    ret = sfc_nor_flash_read(offset, size, *mem);
    if (ret != size) {
        printf("load %s partition fail!\n", partition_name);
        return -1;
    }

    return size;
}

static int nor_load_kernel(struct rtos_boot_os_args *args)
{
    unsigned long offset = args->offset + IMAGE_HEADER_SIZE;
    unsigned long uimage_size = args->size - IMAGE_HEADER_SIZE - GZIP_HEADER_SUM_SIZE;
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
        printf("RTOS nor load kernel: total write length(%d) not equal actual(%d).\n", GZIP_HEADER_SUM_SIZE, ret);
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
            printf("RTOS nor load kernel: total write length(%ld) not equal actual(%d).\n", load_size[i], ret);
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

int load_kernel_partition(struct rtos_boot_os_args *args)
{
#ifdef CONFIG_EMMC_DEVICE
    return mmc_load_kernel(args);
#elif defined(CONFIG_SFC_NAND)
    return nand_load_kernel(args);
#elif defined(CONFIG_SFC_NOR)
    return nor_load_kernel(args);
#else
    printf("RTOS: can't load kernel image, only support emmc_device & sfc_nand & sfc_nor!\n");
    return -EACCES;
#endif
}

int load_partition_data(uint8_t **mem, char *partition_name)
{
#ifdef CONFIG_EMMC_DEVICE
    return mmc_load_partition_data(mem, partition_name);
#elif defined(CONFIG_SFC_NAND)
    return nand_load_partition_data(mem, partition_name);
#elif defined(CONFIG_SFC_NOR)
    return nor_load_partition_data(mem, partition_name);
#else
    printf("RTOS: can't load logo, only support emmc_device & sfc_nand & sfc_nor!\n");
    return -EACCES;
#endif
}

static void load_kernel(void *data)
{
    int ret;
    struct rtos_boot_os_args *os_args = (struct rtos_boot_os_args *)data;

    ret = load_kernel_partition(os_args);
    if (ret < 0)
        panic("RTOS: load kernel failed!!!\n");
}

void quick_boot_logo_and_load_kernel(void *arg)
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

    thread_waiter_init(&wait_load_kernel);
    thread_waiter_init(&uncompress_wait_load);
    image_dst_start = (void *)(os_args->load_addr + IMAGE_HEADER_SIZE);

    // 开启线程
    thread_create("show logo", 8192, show_logo, NULL);
    thread_create("load uImage", 8192, load_kernel, data);
    thread_create("uncompress uImage", 8192, uimage_uncompress_thread, data);

    // gpio_direction_output(GPIO_PB(05),1);     // usb switch to host
    // gpio_set_func(GPIO_PB(05), GPIO_PULL_HIZ);
}
