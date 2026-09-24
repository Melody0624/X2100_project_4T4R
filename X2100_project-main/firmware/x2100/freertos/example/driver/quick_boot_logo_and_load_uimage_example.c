#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <common.h>
#include <os.h>
#include <driver/cache.h>
#include <driver/scboot.h>
#include <uboot_lib.h>
#include <be_byteshift.h>

// #define CONFIG_SECURE_ROOTFS

static thread_waiter_t wait_load_kernel;

#ifdef CONFIG_SECURE_ROOTFS
#define ROOTFS_PARTITION_NAME   "rootfs"
#define ROOTFS_SIG_SIZE         2048
#define ROOTFS_SIZE_INDEX       128
static thread_waiter_t wait_scboot_rootfs;
static thread_waiter_t wait_scboot_kernel;
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

/******************************boot_logo******************************/
#include <cmyk_to_rgb.h>
#include "jpeglib.h"
#include "jerror.h"

#include <driver/fb.h>
#include <driver/backlight.h>
#include <driver/irq.h>

#include <include_bin.h>
INCBIN(jpeg_display, "example/resource/ingenic_logo.jpeg");

static struct fb_info info;
static struct fb_handle *fb0_handle;

/* 初始化fb */
static void enbale_fb_thread(void *data)
{
    fb_enable(fb0_handle);
}

int jpeg_display_to_fb(void)
{
    int brightness;
    struct backlight *lcd_pwm;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPARRAY buffer;
    int row_stride;

    fb0_handle = fb_open("fb0");
    if(fb0_handle == NULL)
        printf("fb0 = NULL\n");

    fb_get_info(fb0_handle, &info);
    if (info.fb_fmt != fb_fmt_RGB888 && info.fb_fmt != fb_fmt_ARGB8888 ) {
        printf("jpeg_display_to_fb: this just support rgb888! you should change this demo\n");
        return -1;
    }
    thread_create("enbale_fb_thread", 1024, enbale_fb_thread, NULL);

    // 绑定标准错误处理结构
    cinfo.err = jpeg_std_error(&jerr);

    // 初始化JPEG对象
    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo,jpeg_displayData, jpeg_displaySize);

    // 读取图像信息
    (void) jpeg_read_header(&cinfo, TRUE);

    // 设定解压缩参数，此处我们将图像长宽缩小为原图的1/2，目前支持1/1,1/2,1/4,1/8
    cinfo.scale_num=1;
    cinfo.scale_denom=1;

    // 开始解压缩图像
    (void) jpeg_start_decompress(&cinfo);

    // 分配缓冲区空间
    row_stride = cinfo.output_width * cinfo.output_components;
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    int display_width = min(cinfo.output_width, info.xres);
    int display_height = min(cinfo.output_height, info.yres);

    int fb_x_offset = (info.xres - display_width) / 2;
    int fb_y_offset = (info.yres - display_height) / 2;

    int jpeg_x_offset = (cinfo.output_width - display_width) / 2;
    int jpeg_y_offset = (cinfo.output_height - display_height) / 2;

    unsigned char *jpeg_rgb_buffer = NULL;
    if (cinfo.out_color_space == JCS_CMYK) {
        jpeg_rgb_buffer = (unsigned char *)malloc(row_stride * sizeof(unsigned char));
    }

    // 定位到fb应该显示的第一行
    unsigned int *fb_mem = info.fb_mem + info.bytes_per_line * fb_y_offset;

    // 定位到jpeg应该显示的第一行（即跳过图片不显示的上半部分）
    int w,h;
    for (h = 0; h < jpeg_y_offset; h++) {
        (void)jpeg_read_scanlines(&cinfo, buffer, 1);
    }

    // 逐行解码jpeg图片，并拷贝到fb
    for (h = 0; h < display_height; h++) {
        (void)jpeg_read_scanlines(&cinfo, buffer, 1);
        unsigned char *jpeg_rgb = buffer[0];

        if (cinfo.out_color_space == JCS_CMYK) {
            cmyk_to_rgb24((unsigned int *)buffer[0], jpeg_rgb_buffer, row_stride, row_stride, cinfo.output_width, 1);
            jpeg_rgb = jpeg_rgb_buffer;
        }

        unsigned int *fb_rgb = fb_mem;
        fb_rgb += fb_x_offset;
        jpeg_rgb += jpeg_x_offset * 3;

        for (w = 0; w < display_width; w++) {
            unsigned int r = jpeg_rgb[w*3 + 0];
            unsigned int g = jpeg_rgb[w*3 + 1];
            unsigned int b = jpeg_rgb[w*3 + 2];

            fb_rgb[w] = (0xff << 24) | (r << 16) | (g << 8) | (b << 0);
        }

        fb_mem = (void *)fb_mem + info.bytes_per_line;
    }

    // 将jpeg解码的行定位到最后，否则jpeg库会报错退出
    cinfo.output_scanline = cinfo.output_height;
    // 等待 fb 开启
    while (!fb_is_enable(fb0_handle)) {
        usleep(100);
    }
    // 显示
    fb_pan_display(fb0_handle, 0);

    // 开启屏幕背光
    lcd_pwm = backlight_open("backlight_pwm0");
    if (lcd_pwm == NULL) {
        printf("backlight_open fail.\n");
    }

    brightness = backlight_get_maxbrightness(lcd_pwm);

    backlight_set_brightness(lcd_pwm, brightness);

    brightness = backlight_get_brightness(lcd_pwm);

    // 结束解压缩操作
    (void) jpeg_finish_decompress(&cinfo);

    // 释放资源
    if (cinfo.out_color_space == JCS_CMYK) {
        free(jpeg_rgb_buffer);
    }
    jpeg_destroy_decompress(&cinfo);

    return 0;
}

static void show_logo(void *data)
{
    jpeg_display_to_fb();

    thread_waiter_wait(&wait_load_kernel);

#ifdef CONFIG_SECURE_ROOTFS
    thread_waiter_wait(&wait_scboot_rootfs);
#endif

    release_all_resources();

    jump_to_image_linux(data);
}

/******************************load_kernel******************************/

#include <spl_rtos_argument.h>
#include <zlib/zlib.h>

#define IMAGE_HEADER_SIZE       64
#define UNC_LEN                 0xf00000

static inline void *is_address_valid(unsigned long address)
{
    if (address > CKSEG0 && address < CKSEG2)
        return (void *)address;
    else
        return NULL;
}

#ifdef CONFIG_EMMC_DEVICE

#define MMC_SECTOR_SIZE                 (512)

uint32_t mmc_device_block_read(uint64_t address, uint32_t length, void *buffer);

static void* mmc_load_kernel(struct rtos_boot_os_args *args)
{
    int size_sector = (args->size + MMC_SECTOR_SIZE - 1) / MMC_SECTOR_SIZE;
    int size = size_sector * MMC_SECTOR_SIZE;
    void *load_image = malloc(size);
    if (!load_image) {
        printf("malloc load_image size %d fail\n", size);
        return NULL;
    }

    int ret = mmc_device_block_read(args->offset, size, load_image);
    if (ret < 0) {
        printf("RTOS mmc load kernel failed.\n");
        return NULL;
    }

    return load_image;
}
#endif

#ifdef CONFIG_SFC_NAND
#include <driver/sfc_nand.h>


static void* nand_load_kernel(struct rtos_boot_os_args *args)
{
    uint32_t offset = args->offset;
    void *load_image = malloc(args->size);
    if (!load_image) {
        printf("malloc load_image size %d fail\n", args->size);
        return NULL;
    }

    int ret = sfc_nand_flash_read_check_badblock(&offset, args->size, load_image);
    if (ret != args->size) {
        printf("RTOS nand load kernel: total write length(%d) not equal actual(%d).\n", args->size, ret);
        return NULL;
    }

    return load_image;
}
#endif

#ifdef CONFIG_SFC_NOR
#include <driver/sfc_nor.h>
static void* nor_load_kernel(struct rtos_boot_os_args *args)
{
    void *load_image = malloc(args->size);
    if (!load_image) {
        printf("malloc load_image size %d fail\n", args->size);
        return NULL;
    }

    int ret = sfc_nor_flash_read(args->offset, args->size, load_image);
    if (ret != args->size) {
        printf("RTOS nor load kernel: total write length(%d) not equal actual(%d).\n", args->size, ret);
        return NULL;
    }

    return load_image;
}
#endif

#ifdef CONFIG_SECURE_ROOTFS

static int get_partition_information_by_name(char *partition_name, uint64_t *offset, uint64_t *size)
{
    int ret;
#ifdef CONFIG_EMMC_DEVICE
    ret = get_mmc_partition_information_by_name(partition_name, offset, size);
#elif defined(CONFIG_SFC_NAND)
    ret = get_nand_partition_information_by_name(partition_name, (uint32_t *)offset, (uint32_t *)size);
#elif defined(CONFIG_SFC_NOR)
    ret = get_nor_partition_information_by_name(partition_name, (uint32_t *)offset, (uint32_t *)size);
#else
    printf("RTOS: can't load partition, only support emmc_device & sfc_nand & sfc_nor!\n");
    return -EACCES;
#endif

    return ret;
}

static int load_partition_data(uint32_t offset, uint32_t size, uint8_t *buf)
{
#ifdef CONFIG_EMMC_DEVICE
    int size_sector = (size + MMC_SECTOR_SIZE - 1) / MMC_SECTOR_SIZE;
    int size = size_sector * MMC_SECTOR_SIZE;
    return mmc_device_block_read(offset, size, (void *)buf);
#elif defined(CONFIG_SFC_NAND)
    return sfc_nand_flash_read(offset, size, buf);
#elif defined(CONFIG_SFC_NOR)
    return sfc_nor_flash_read(offset, size, buf);
#else
    printf("RTOS: can't load logo, only support emmc_device & sfc_nand & sfc_nor!\n");
    return -EACCES;
#endif
}

static void scboot_rootfs(void *data)
{
    int ret;
    uint64_t offset, size;
    uint32_t *rootfs_buf = (uint32_t *)data;
    uint32_t sig_size = ROOTFS_SIG_SIZE;

    ret = get_partition_information_by_name(ROOTFS_PARTITION_NAME, &offset, &size);
    if (ret < 0) {
        printf("get %s partition information fail!\n", ROOTFS_PARTITION_NAME);
        return;
    }

    size = rootfs_buf[ROOTFS_SIZE_INDEX];
    ret = load_partition_data(offset, size, (uint8_t *)rootfs_buf + sig_size);
    if (ret != size) {
        printf("load %s partition fail!\n", ROOTFS_PARTITION_NAME);
        return;
    }

    thread_waiter_wait(&wait_scboot_kernel);

    ret = secure_scboot((void *)rootfs_buf, (void *)rootfs_buf);
    if(ret) {
        printf("Error check secure rootfs\n");
        hang();
    }

    thread_waiter_wakeup(&wait_scboot_rootfs);
}

#endif

#ifdef CONFIG_ZLIB
#include <zlib/src/gunzip.h>
#elif CONFIG_LZMA
#include "lzma/src/LzmaLib.h"
#endif

int load_kernel_partition(struct rtos_boot_os_args *args, uint8_t *load_image)
{
    if (load_image == NULL) {
        printf("load_kernel error\n");
        return -EACCES;
    }

    unsigned long  uimage_size = args->size - IMAGE_HEADER_SIZE;
    void *dst = (void *)(args->load_addr + IMAGE_HEADER_SIZE);
#ifdef CONFIG_SECURE_ROOTFS
    uint32_t *rootfs_buf;
    uint32_t *sig_buf;

    sig_buf = (uint32_t *)&load_image[uimage_size];
    rootfs_buf = memalign(4096, ALIGN(sig_buf[ROOTFS_SIZE_INDEX] + ROOTFS_SIG_SIZE, cache_line_size()));
    if (!rootfs_buf) {
        printf("alloc rootfs buf failed!\n");
        return -1;
    }

    memcpy(rootfs_buf, sig_buf, ROOTFS_SIG_SIZE);
    thread_create("scboot rootfs", 8192, scboot_rootfs, rootfs_buf);
#endif

#ifdef CONFIG_SCBOOT

#ifdef CONFIG_SECURE_BOOT_KERNEL
    int ret = secure_scboot((void *)(load_image), (void *)(load_image));
    if(ret) {
        printf("Error spl secure load kernel.\n");
        hang();
    }
    image_header_t *image_header = (image_header_t *)load_image;
    uimage_size = get_unaligned_be32(&image_header->ih_size) - IMAGE_HEADER_SIZE;
#endif

#ifdef CONFIG_SECURE_ROOTFS
    thread_waiter_wakeup(&wait_scboot_kernel);
#endif

#endif

#ifdef CONFIG_ZLIB
    if (gunzip(dst, UNC_LEN, load_image + IMAGE_HEADER_SIZE, &uimage_size, NULL, NULL) != 0) {
        printf("GUNZIP: uncompress, out-of-mem or overwrite "
            "error - must RESET board to recover\n");
        return -EACCES;
    }

#elif defined(CONFIG_LZMA)
    unsigned long input_size = 0;
    unsigned long output_size = 0;
    load_image += IMAGE_HEADER_SIZE;

    for (int i=0; i<4; i++) {
        output_size |= (unsigned long)load_image[uimage_size - 4 + i] << (i * 8);
    }

    input_size = uimage_size - 13;

    int ret = LzmaUncompress((unsigned char *)dst, &output_size, load_image + 13, &input_size, load_image, 5);
    if (ret != 0) {
        printf("lzma uncompress error, ret = %d\n", ret);
        return -EACCES;
    }
#else
    printf("RTOS not select third party(glib or lzma)!\n");
    return -EACCES;
#endif
    return 0;
}

static void load_kernel(void *data)
{
    int ret;
    struct rtos_boot_os_args *os_args = (struct rtos_boot_os_args *)data;

    unsigned char *load_image;
#ifdef CONFIG_SECURE_ROOTFS
    os_args->size += ROOTFS_SIG_SIZE;
#endif

#ifdef CONFIG_EMMC_DEVICE
    load_image = (unsigned char *)mmc_load_kernel(os_args);
#elif defined(CONFIG_SFC_NAND)
    load_image = (unsigned char *)nand_load_kernel(os_args);
#elif defined(CONFIG_SFC_NOR)
    load_image = (unsigned char *)nor_load_kernel(os_args);
#else
    printf("RTOS: can't load kernel image, only support emmc_device & sfc_nand & sfc_nor!\n");
    return -EACCES;
#endif

#ifdef CONFIG_SECURE_ROOTFS
    os_args->size -= ROOTFS_SIG_SIZE;
#endif

    ret = load_kernel_partition(os_args, load_image);
    if (ret < 0)
        panic("RTOS: load kernel failed!!!\n");

    thread_waiter_wakeup(&wait_load_kernel);
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
#ifdef CONFIG_SECURE_ROOTFS
    thread_waiter_init(&wait_scboot_kernel);
    thread_waiter_init(&wait_scboot_rootfs);
#endif

    // 开启线程
    thread_create("show logo", 8192, show_logo, data);
    thread_create("boot uImage", 8192, load_kernel, data);
}