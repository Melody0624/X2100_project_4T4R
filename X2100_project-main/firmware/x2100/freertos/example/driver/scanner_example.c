#include <os.h>
#include <stdio.h>
#include <config.h>
#include <common.h>
#include <driver/pwm.h>
#include <driver/gpio.h>
#include <driver/cache.h>
#include <jpege_encoder.h>
#include <jpegd_decoder.h>
#include <driver/fb.h>
#include <driver/irq.h>
#include <driver/camera.h>

#include <driver/systick.h>
#include <devices/afe_ht82v38.h>

#ifdef CONFIG_DFS
#include <dfs.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <dfs_posix.h>
#endif

#define SCANNER_DISPLAY_ON_FB         1
#define SCANNER_CURRENT_DPI           600
#define SCANNER_JPEG_QUALITY          60
#define SCANNER_CHANNELS              1
#define SCANNER_LIGHT_MODE            SCANNER_LIGHT_RGB
#define SCANNER_OUTPUT_FORMAT         SCANNER_FORMAT_JPG
#define SCANNER_OUTPUT_JPG_PATH       "/mmcblk0p0/scanner.jpg"
#define SCANNER_OUTPUT_RAW_PATH       "/mmcblk0p0/scanner.raw"

/* AFE PGA 自动校准参数 */
#define SCANNER_AFE_PGA_MIN           1
#define SCANNER_AFE_PGA_MAX           0x3f

/* AFE PGA 自动校准时的 增益调整比例上下限 */
#define SCANNER_AFE_PGA_RATIO_MIN_Q12 2867  /* 0.70 */
#define SCANNER_AFE_PGA_RATIO_MAX_Q12 5734  /* 1.40 */

/* AFE OFFSET 自动校准参数 */
#define SCANNER_AFE_OFFSET_MIN        0x000
#define SCANNER_AFE_OFFSET_MAX        0x1ff
#define SCANNER_AFE_OFFSET_TARGET     2
#define SCANNER_AFE_OFFSET_TOL        3
#define SCANNER_AFE_OFFSET_STEP_DIV   4
#define SCANNER_AFE_OFFSET_STEP_MAX   16

/* LED 自动步进调节 */
#define SCANNER_LED_STEP_L            2
#define SCANNER_LED_STEP_M            3
#define SCANNER_LED_STEP_H            4
#define SCANNER_LED_STEP_MAX_STEPS    10
#define SCANNER_LED_CALIB_HEIGHT      200

/* 软件黑电平扣除 + 软增益拉伸 */
#define SCANNER_RGB_BLACK_R           16
#define SCANNER_RGB_BLACK_G           16
#define SCANNER_RGB_BLACK_B           16
#define SCANNER_RGB_GAIN_R            300
#define SCANNER_RGB_GAIN_G            300
#define SCANNER_RGB_GAIN_B            300
#define SCANNER_RGB_GAIN_SHIFT        8

/* 校准高度 通过调节校准高度 */
#define SCANNER_CALIB_HEIGHT          600
/* 设置目标亮度 250 表示亮度是采集的250% */
#define SCANNER_CALIB_TARGET          250

#define SCANNER_ROLL_PULSE_GPIO       GPIO_PC(12)
#define SCANNER_ROLL_PULSE_IRQ_TYPE   IRQ_TYPE_EDGE_RISING

/* 无滚动脉冲超过该时间视为滚动结束 (ms) */
#define SCANNER_ROLL_PULSE_STOP_TIMEOUT_MS  500
/* 轮询等待粒度 (ms), 影响停止响应速度 */
#define SCANNER_ROLL_PULSE_WAIT_SLICE_MS    20
/* 采集校准时跳过的行数 */
#define SCANNER_SKIP_LINE_COUNT             20
/* 每chunk块包含的行数 */
#define SCANNER_ROLL_PULSE_CHUNK_LINES      256

#define SCANNER_CALIB_MAGIC                 0x5343414cU     /* 'SCAL' */
#define SCANNER_COL_CALIB_MAGIC             0x53434f4cU     /* 'SCOL' */

/* 存储校准bin路径 */
#define SCANNER_CALIB_PATH_FMT              "/mmcblk0p0/scanner_calib_%ddpi.bin"
#define SCANNER_CALIB_COL_PATH_FMT          "/mmcblk0p0/scanner_calib_col_%ddpi.bin"
#define SCANNER_CALIB_COL_MONO_PATH_FMT     "/mmcblk0p0/scanner_calib_col_mono_%ddpi.bin"

enum scanner_output_format {
    SCANNER_FORMAT_JPG = 0,
    SCANNER_FORMAT_RAW,
};

enum scanner_light_mode {
    SCANNER_LIGHT_RGB = 0,
    SCANNER_LIGHT_MONO,
};

struct scanner_buf_chunk {
    unsigned char *data;
    int capacity_lines;
    int used_lines;
    struct scanner_buf_chunk *next;
};

struct scanner_config {
    enum scanner_output_format format;
    enum scanner_light_mode light_mode;
    int capture_height;
    int buffer_height;
    size_t buffer_bytes;
    int channels;
    unsigned char *scanner_buf;
    unsigned int scanner_width;
    int dpi;

    struct scanner_buf_chunk *chunk_head;
    struct scanner_buf_chunk *chunk_tail;
};

/* scanner实际处理的黑场数据 */
static int scanner_rgb_black[3] = {
    SCANNER_RGB_BLACK_R, SCANNER_RGB_BLACK_G, SCANNER_RGB_BLACK_B
};

/* scanner实际处理的软件增益 */
static int scanner_rgb_gain_q8[3] = {
    SCANNER_RGB_GAIN_R, SCANNER_RGB_GAIN_G, SCANNER_RGB_GAIN_B
};

/* scanner列实际处理的黑场数据和软件增益 */
static unsigned short *scanner_col_black[3];
static unsigned short *scanner_col_gain_q8[3];
static unsigned int scanner_col_width;
static int scanner_col_valid;

static semaphore_t scanner_roll_sem;
static volatile uint32_t scanner_roll_last_ms;
static int scanner_roll_irq_inited;

struct scanner_capture_ctx {
    struct camera_device *camera;
    const struct camera_info *info;
    struct scanner_config *cfg;
    semaphore_t done;
};

static size_t scanner_frame_bytes_per_pixel(const struct camera_info *info)
{
    size_t bpp = 0;

    if (!info || !info->width || !info->height)
        return 0;

    if (info->line_length >= info->width)
        bpp = info->line_length / info->width;

    if (bpp == 0) {
        size_t pixels = (size_t)info->width * info->height;
        if (pixels)
            bpp = info->frame_size / pixels;
    }

    if (bpp == 0) {
        switch (info->data_fmt) {
        case CAMERA_PIX_FMT_GREY:
            bpp = 1;
            break;
        case CAMERA_PIX_FMT_RGB565:
            bpp = 2;
            break;
        case CAMERA_PIX_FMT_RGB24:
        case CAMERA_PIX_FMT_RBG24:
        case CAMERA_PIX_FMT_GBR24:
        case CAMERA_PIX_FMT_GRB24:
        case CAMERA_PIX_FMT_BGR24:
            bpp = 3;
            break;
        default:
            break;
        }
    }

    return bpp;
}

#ifdef CONFIG_DFS
static int scanner_write_file(const char *path, const void *data, size_t size)
{
    int fd;
    size_t remaining = size;
    const unsigned char *ptr = (const unsigned char *)data;

    if (!path || !data || size == 0)
        return -EINVAL;

    mkdir("/test", 0666);
    fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0);
    if (fd < 0) {
        printf("scanner demo: open %s failed\n", path);
        return -1;
    }

    while (remaining) {
        size_t chunk = remaining > (1024 * 1024) ? (1024 * 1024) : remaining;
        int written = write(fd, ptr, chunk);
        if (written <= 0) {
            printf("scanner demo: write %s failed\n", path);
            close(fd);
            return -1;
        }
        remaining -= (size_t)written;
        ptr += written;
    }

    close(fd);
    return 0;
}

static int scanner_read_file(const char *path, void *data, size_t size)
{
    int fd;
    int ret;

    if (!path || !data || size == 0)
        return -EINVAL;

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return -1;

    ret = read(fd, data, size);
    close(fd);

    return ret;
}

static int scanner_read_all(int fd, void *buf, size_t size)
{
    unsigned char *ptr = (unsigned char *)buf;
    size_t remaining = size;

    while (remaining) {
        int ret = read(fd, ptr, remaining);
        if (ret <= 0)
            return -1;
        remaining -= (size_t)ret;
        ptr += ret;
    }

    return 0;
}

#endif
/*----------------------------------------------------------------------------*/


/*-------------------------------编码-----------------------------------------*/
struct scanner_jpeg_output {
    struct jpege_encoder *encoder;
    struct jpege_encoder_output *out;
    void *buf;
};

static void scanner_free_jpeg_output(struct scanner_jpeg_output *out)
{
    if (!out)
        return;

    if (out->out && out->encoder)
        jpege_encoder_free_output_buf(out->out);
    if (out->encoder)
        jpege_encoder_deinit(out->encoder);
    if (out->buf)
        free(out->buf);

    out->encoder = NULL;
    out->out = NULL;
    out->buf = NULL;
}

static int scanner_encode_jpeg(const struct scanner_config *cfg, struct scanner_jpeg_output *out)
{
    void *buf = NULL;
    void *src = NULL;
    size_t src_size = 0;
    int ret;

    struct jpege_encoder *encoder;
    struct jpege_encoder_param param;
    struct jpege_encoder_output *out_buf;

    if (!cfg || !out)
        return -EINVAL;

    memset(out, 0, sizeof(*out));
    memset(&param, 0, sizeof(param));
    param.width = (int)cfg->scanner_width;
    param.height = cfg->capture_height;
    param.quality = SCANNER_JPEG_QUALITY;

    if (cfg->light_mode == SCANNER_LIGHT_MONO) {
        size_t y_size = (size_t)cfg->scanner_width * (size_t)cfg->capture_height;
        size_t uv_size = y_size / 2;
        size_t nv12_size = y_size + uv_size;

        buf = malloc(nv12_size);
        if (!buf)
            return -ENOMEM;

        memcpy(buf, cfg->scanner_buf, y_size);
        memset((unsigned char *)buf + y_size, 0x80, uv_size);

        param.in_fmt = JPEGE_PIX_FMT_NV12;
        src = buf;
        src_size = nv12_size;
    } else {
        param.in_fmt = JPEGE_PIX_FMT_RGB_888;
        src = cfg->scanner_buf;
        src_size = (size_t)cfg->scanner_width * (size_t)cfg->capture_height * 3;
    }

    encoder = jpege_encoder_init(&param);
    if (!encoder) {
        if (buf)
            free(buf);
        return -1;
    }

    out_buf = jpege_encoder_alloc_output_buf(encoder);
    if (!out_buf) {
        jpege_encoder_deinit(encoder);
        if (buf)
            free(buf);
        return -1;
    }

    ret = jpege_encoder_encode(encoder, src, (int)src_size, out_buf);
    if (ret < 0) {
        printf("scanner demo: jpeg encode failed\n");
        jpege_encoder_free_output_buf(out_buf);
        jpege_encoder_deinit(encoder);
        if (buf)
            free(buf);
        return ret;
    }

    out->encoder = encoder;
    out->out = out_buf;
    out->buf = buf;
    return 0;
}
/*---------------------------------------------------------------------------------*/


/*-----------------------------解码&显示-------------------------------------------*/
#if SCANNER_DISPLAY_ON_FB
static void scanner_draw_data_to_fb_scaled(struct fb_info *fb_info, struct jpegd_decoder_output *out)
{
    if (!fb_info || !fb_info->fb_mem || !out)
        return;

    if (fb_info->fb_fmt != fb_fmt_ARGB8888 && fb_info->fb_fmt != fb_fmt_RGB888) {
        printf("scanner demo: unsupported fb fmt %d\n", fb_info->fb_fmt);
        return;
    }

    int src_w = out->actual_width;
    int src_h = out->actual_height;
    int dst_w = (int)fb_info->xres;
    int dst_h = (int)fb_info->yres;
    int out_w = dst_w;
    int out_h = dst_h;
    int dst_x = 0;
    int dst_y = 0;

    if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
        return;

    long long fit_w = (long long)dst_h * src_w / src_h;
    long long fit_h = (long long)dst_w * src_h / src_w;

    if (fit_w <= dst_w) {
        out_w = (int)fit_w;
        out_h = dst_h;
    } else {
        out_w = dst_w;
        out_h = (int)fit_h;
    }

    if (out_w < 1)
        out_w = 1;
    if (out_h < 1)
        out_h = 1;

    dst_x = (dst_w - out_w) / 2;
    dst_y = (dst_h - out_h) / 2;
    memset(fb_info->fb_mem, 0, fb_info->bytes_per_frame);

    int fb_bpp = (fb_info->fb_fmt == fb_fmt_ARGB8888) ? 4 : 3;
    int src_stride = out->width * 4;

    for (int y = 0; y < out_h; y++) {
        int src_y = (int)((long long)y * src_h / out_h);
        unsigned char *fb_row = (unsigned char *)fb_info->fb_mem +
            (dst_y + y) * fb_info->bytes_per_line + dst_x * fb_bpp;
        unsigned char *src_row = (unsigned char *)out->data + src_y * src_stride;

        for (int x = 0; x < out_w; x++) {
            int src_x = (int)((long long)x * src_w / out_w);
            unsigned char *src = src_row + src_x * 4;

            if (fb_info->fb_fmt == fb_fmt_ARGB8888) {
                fb_row[x * 4 + 0] = src[0];
                fb_row[x * 4 + 1] = src[1];
                fb_row[x * 4 + 2] = src[2];
                fb_row[x * 4 + 3] = src[3];
            } else {
                fb_row[x * 3 + 0] = src[2];
                fb_row[x * 3 + 1] = src[1];
                fb_row[x * 3 + 2] = src[0];
            }
        }
    }
}

static int scanner_decode_jpeg(const void *jpeg_data, unsigned int jpeg_size,
    int width, int height, struct jpegd_decoder **decoder,
    struct jpegd_decoder_output **out)
{
    struct jpegd_decoder *local_decoder;
    struct jpegd_decoder_output *local_out;

    struct jpegd_decoder_param param = {
        .width = width,
        .height = height,
        .out_fmt = JPEGD_PIX_FMT_BGRA_8888,
        .crop = 1,
    };

    if (!jpeg_data || jpeg_size == 0 || !decoder || !out)
        return -EINVAL;

    local_decoder = jpegd_decoder_init(&param);
    if (!local_decoder)
        return -1;

    local_out = jpegd_decoder_alloc_output_buf(local_decoder);
    if (!local_out) {
        jpegd_decoder_deinit(local_decoder);
        return -1;
    }

    if (jpegd_decoder_decode(local_decoder, (void *)jpeg_data, (int)jpeg_size, local_out) < 0) {
        printf("scanner demo: jpeg decode failed\n");
        jpegd_decoder_free_output_buf(local_out);
        jpegd_decoder_deinit(local_decoder);
        return -1;
    }

    *decoder = local_decoder;
    *out = local_out;
    return 0;
}

static void scanner_free_decoded_jpeg(struct jpegd_decoder *decoder,
    struct jpegd_decoder_output *out)
{
    if (out && decoder)
        jpegd_decoder_free_output_buf(out);
    if (decoder)
        jpegd_decoder_deinit(decoder);
}

static int scanner_display_decoded_to_fb(const struct jpegd_decoder_output *out)
{
    struct fb_info fb_info;
    struct fb_handle *fb;

    if (!out)
        return -EINVAL;

    fb = fb_open("fb0");
    if (!fb) {
        printf("scanner demo: fb0 open failed\n");
        return -1;
    }

    fb_get_info(fb, &fb_info);

    fb_enable(fb);
    while (!fb_is_enable(fb))
        mdelay(1);

    scanner_draw_data_to_fb_scaled(&fb_info, (struct jpegd_decoder_output *)out);

    flush_dcache_force((unsigned long)fb_info.fb_mem, fb_info.bytes_per_frame);
    fb_pan_display(fb, 0);
    return 0;
}

#endif
/*-------------------------------------------------------------------------------*/

/*-----------------------------scanner init--------------------------------------*/
static void scanner_pwm_stop(int pwm_id)
{
    if (pwm_id < 0)
        return;

    pwm_set_level(pwm_id, 0);
    pwm_release(pwm_id);
}

struct camera_device* scanner_init(struct camera_info* camera_info)
{
    int ret;
    struct camera_device *camera;

    camera = camera_detect(0);
    if (!camera) {
        printf("camera not found\n");
        return NULL;
    }

    camera_info = camera_get_info(camera);
    assert(camera_info);
    printf("camera found %s (%dx%d)\n", camera_info->name, camera_info->width, camera_info->height);

    ret = camera_power_on(camera);
    if (ret < 0) {
        printf("camera failed to power on\n");
        goto power_off;
    }

    return camera;

power_off:
    camera_power_off(camera);
    return NULL;
}
/*-------------------------------------------------------------------------------*/


/*------------------------------rgb模式------------------------------------------*/
void scanner_rgb_mode(unsigned char *dest, unsigned char* src, size_t total_pixels)
{
    int i, j;
    int bytes_per_pixel = 4;
    size_t plane_pixels = total_pixels / 3;
    size_t scanner_width = total_pixels;
    size_t color_plane_size = plane_pixels * bytes_per_pixel;

    unsigned char *frame_data = (unsigned char *)src;
    unsigned char *r_plane = frame_data;
    unsigned char *g_plane = frame_data + color_plane_size;
    unsigned char *b_plane = frame_data + color_plane_size * 2;

    /* 输入为三平面(R/G/B)的打包数据，每个plane占plane_pixels个像素。
     * 输出缓冲以scanner_width为一行宽度，最终布局为3个plane并排。
     * SCANNER_CHANNELS=3:
     *         分别取r g b plane的前三个字节
     *         放在对应的dest的不同通道的对应分量中:
     *           如:r[0]放在第一个像素的r分量中
     *              r[1]放在第scanner_width个像素的r分亮中
     *              r[2]放在第2*scanner_width个像素的r分亮中
     *              g b 同理;
     *
     * SCANNER_CHANNELS=1:
     *         分别取r g b plane的前三个字节
     *         放在对应的dest中的前三个像素的对应分量中:
     *            如: r[0] r[1] r[2]放在dest[0] dest[3] dest[6]
     *                g[0] g[1] g[2]放在dest[1] dest[4] dest[7]
     *                b[0] b[1] b[2]放在dest[2] dest[5] dest[8]
     */

    /**
     * 原始信号 = 真实信号 + 暗电流噪声
     * 黑场信号 = 0 + 暗电流噪声
     * 校正后信号 = 原始信号 - 黑场信号
     *            = (真实信号 + 噪声) - 噪声
     *            = 真实信号
     */

    for (i = 0; i < plane_pixels; i++) {
        size_t src_offset = i * bytes_per_pixel;
        size_t dst_offset;

        if (SCANNER_CHANNELS == 3) {
            dst_offset = i * 3;
            for (j = 0; j < 3; j++) {
                unsigned char r = r_plane[src_offset + j];
                unsigned char g = g_plane[src_offset + j];
                unsigned char b = b_plane[src_offset + j];

                int rr = (int)r - scanner_rgb_black[0];
                int gg = (int)g - scanner_rgb_black[1];
                int bb = (int)b - scanner_rgb_black[2];

                if (rr < 0) rr = 0;
                if (gg < 0) gg = 0;
                if (bb < 0) bb = 0;

                // 列校准
                // 处理不均匀的蒙层
                rr = (rr * scanner_rgb_gain_q8[0]) >> SCANNER_RGB_GAIN_SHIFT;
                gg = (gg * scanner_rgb_gain_q8[1]) >> SCANNER_RGB_GAIN_SHIFT;
                bb = (bb * scanner_rgb_gain_q8[2]) >> SCANNER_RGB_GAIN_SHIFT;

                if (rr > 255) rr = 255;
                if (gg > 255) gg = 255;
                if (bb > 255) bb = 255;

                dest[(size_t)j * scanner_width + dst_offset + 0] = (unsigned char)rr;
                dest[(size_t)j * scanner_width + dst_offset + 1] = (unsigned char)gg;
                dest[(size_t)j * scanner_width + dst_offset + 2] = (unsigned char)bb;
            }
        } else {
            dst_offset = i * 9;
            for (j = 0; j < 3; j++) {
                unsigned char r = r_plane[src_offset + j];
                unsigned char g = g_plane[src_offset + j];
                unsigned char b = b_plane[src_offset + j];

                if (scanner_col_valid && scanner_col_width) {
                    unsigned int col = (unsigned int)i * 3U + (unsigned int)j;
                    if (col < scanner_col_width) {
                        unsigned short br = scanner_col_black[0][col];
                        unsigned short bg = scanner_col_black[1][col];
                        unsigned short bbk = scanner_col_black[2][col];
                        unsigned short gr = scanner_col_gain_q8[0][col];
                        unsigned short gg = scanner_col_gain_q8[1][col];
                        unsigned short gb = scanner_col_gain_q8[2][col];
                        int rr = (int)r - (int)br;
                        int gg2 = (int)g - (int)bg;
                        int bb2 = (int)b - (int)bbk;

                        if (rr < 0) rr = 0;
                        if (gg2 < 0) gg2 = 0;
                        if (bb2 < 0) bb2 = 0;

                        rr = (rr * gr) >> SCANNER_RGB_GAIN_SHIFT;
                        gg2 = (gg2 * gg) >> SCANNER_RGB_GAIN_SHIFT;
                        bb2 = (bb2 * gb) >> SCANNER_RGB_GAIN_SHIFT;

                        if (rr > 255) rr = 255;
                        if (gg2 > 255) gg2 = 255;
                        if (bb2 > 255) bb2 = 255;

                        dest[dst_offset + (size_t)j * 3 + 0] = (unsigned char)rr;
                        dest[dst_offset + (size_t)j * 3 + 1] = (unsigned char)gg2;
                        dest[dst_offset + (size_t)j * 3 + 2] = (unsigned char)bb2;
                        continue;
                    }
                }

                int rr = (int)r - scanner_rgb_black[0];
                int gg = (int)g - scanner_rgb_black[1];
                int bb = (int)b - scanner_rgb_black[2];

                if (rr < 0) rr = 0;
                if (gg < 0) gg = 0;
                if (bb < 0) bb = 0;

                // 列校准
                // 处理不均匀的蒙层
                rr = (rr * scanner_rgb_gain_q8[0]) >> SCANNER_RGB_GAIN_SHIFT;
                gg = (gg * scanner_rgb_gain_q8[1]) >> SCANNER_RGB_GAIN_SHIFT;
                bb = (bb * scanner_rgb_gain_q8[2]) >> SCANNER_RGB_GAIN_SHIFT;

                if (rr > 255) rr = 255;
                if (gg > 255) gg = 255;
                if (bb > 255) bb = 255;

                dest[dst_offset + (size_t)j * 3 + 0] = (unsigned char)rr;
                dest[dst_offset + (size_t)j * 3 + 1] = (unsigned char)gg;
                dest[dst_offset + (size_t)j * 3 + 2] = (unsigned char)bb;
            }
        }
    }
}
/*-------------------------------------------------------------------------------*/


/*---------------------------------mono模式--------------------------------------*/
void scanner_mono_mode(unsigned char* dest, unsigned char* src, size_t size, size_t bytes_per_pixel)
{
    size_t i;

    if (!dest || !src || size == 0)
        return;

    if (bytes_per_pixel == 0)
        bytes_per_pixel = 1;

    if ((size % 3 == 0)) {
        size_t plane_pixels = size / 3;
        size_t plane_bytes = plane_pixels * bytes_per_pixel;
        const unsigned char *r_plane = src;
        const unsigned char *g_plane = src + plane_bytes;
        const unsigned char *b_plane = src + plane_bytes * 2;

        for (i = 0; i < plane_pixels; ++i) {
            size_t src_offset = i * bytes_per_pixel;
            size_t dst_offset = i * 3;
            int rr = (int)r_plane[src_offset];
            int gg = (int)g_plane[src_offset];
            int bb = (int)b_plane[src_offset];

            if (scanner_col_valid && scanner_col_width) {
                unsigned int col_r = (unsigned int)i * 3U + 0;
                unsigned int col_g = col_r + 1U;
                unsigned int col_b = col_r + 2U;

                if (col_r < scanner_col_width) {
                    rr -= (int)scanner_col_black[0][col_r];
                    rr = (rr * (int)scanner_col_gain_q8[0][col_r]) >> SCANNER_RGB_GAIN_SHIFT;
                }
                if (col_g < scanner_col_width) {
                    gg -= (int)scanner_col_black[1][col_g];
                    gg = (gg * (int)scanner_col_gain_q8[1][col_g]) >> SCANNER_RGB_GAIN_SHIFT;
                }
                if (col_b < scanner_col_width) {
                    bb -= (int)scanner_col_black[2][col_b];
                    bb = (bb * (int)scanner_col_gain_q8[2][col_b]) >> SCANNER_RGB_GAIN_SHIFT;
                }
            } else {
                rr -= scanner_rgb_black[0];
                gg -= scanner_rgb_black[1];
                bb -= scanner_rgb_black[2];

                rr = (rr * scanner_rgb_gain_q8[0]) >> SCANNER_RGB_GAIN_SHIFT;
                gg = (gg * scanner_rgb_gain_q8[1]) >> SCANNER_RGB_GAIN_SHIFT;
                bb = (bb * scanner_rgb_gain_q8[2]) >> SCANNER_RGB_GAIN_SHIFT;
            }

            if (rr < 0) rr = 0;
            if (gg < 0) gg = 0;
            if (bb < 0) bb = 0;
            if (rr > 255) rr = 255;
            if (gg > 255) gg = 255;
            if (bb > 255) bb = 255;

            unsigned int y = (rr * 77 + gg * 150 + bb * 29) >> 8;

            dest[dst_offset + 0] = (unsigned char)y;
            dest[dst_offset + 1] = (unsigned char)y;
            dest[dst_offset + 2] = (unsigned char)y;
        }
        return;
    }

    /* 直接在采集时做灰度（Y），避免额外RGB缓冲 */
    for (i = 0; i < size; ++i) {
        size_t src_offset = i * bytes_per_pixel;
        unsigned int r = src[src_offset + 0];
        unsigned int g = (bytes_per_pixel > 1) ? src[src_offset + 1] : r;
        unsigned int b = (bytes_per_pixel > 2) ? src[src_offset + 2] : r;

        /* 近似BT.601: Y = 0.299R + 0.587G + 0.114B */
        unsigned int y = (r * 77 + g * 150 + b * 29) >> 8;
        dest[i] = (unsigned char)y;
    }
}
/*-------------------------------------------------------------------------------*/


/*--------------------------滚轮相关---------------------------------------------*/
static void scanner_roll_irq_handler(int irq, void *data)
{
    uint32_t now_ms = (uint32_t)systick_get_time_ms();

    scanner_roll_last_ms = now_ms;
    semaphore_post(&scanner_roll_sem);
}

static void scanner_roll_irq_init(void)
{
    int ret;

    if (scanner_roll_irq_inited)
        return;

    ret = gpio_request(SCANNER_ROLL_PULSE_GPIO, "scanner_roll");
    if (ret < 0) {
        printf("scanner demo: request roll gpio failed\n");
        return;
    }

    gpio_direction_input(SCANNER_ROLL_PULSE_GPIO);

    scanner_roll_last_ms = 0;
    semaphore_init(&scanner_roll_sem, 0);

    int irq = gpio_to_irq(SCANNER_ROLL_PULSE_GPIO);
    if (irq < 0) {
        printf("scanner demo: gpio_to_irq failed %d\n", irq);
        return;
    }

    request_irq(irq, SCANNER_ROLL_PULSE_IRQ_TYPE, scanner_roll_irq_handler, "scanner-roll", NULL);

    scanner_roll_irq_inited = 1;
}
/*-------------------------------------------------------------------------------*/


/*--------------------------采集裸图---------------------------------------------*/
/**
 * @brief 1. 可根据不同的采集方式修改（以滚轮滚动采集一行为例
 */
static void scanner_chunks_reset(struct scanner_config *cfg)
{
    struct scanner_buf_chunk *chunk;

    if (!cfg)
        return;

    chunk = cfg->chunk_head;
    while (chunk) {
        struct scanner_buf_chunk *next = chunk->next;
        if (chunk->data)
            free(chunk->data);
        free(chunk);
        chunk = next;
    }

    cfg->chunk_head = NULL;
    cfg->chunk_tail = NULL;
}

static int scanner_chunk_get_line(struct scanner_config *cfg,
                                     size_t bytes_per_line,
                                     unsigned char **out_line)
{
    struct scanner_buf_chunk *chunk;

    if (!cfg || !out_line || bytes_per_line == 0)
        return -EINVAL;

    chunk = cfg->chunk_tail;
    if (!chunk || chunk->used_lines >= chunk->capacity_lines) {
        int lines = SCANNER_ROLL_PULSE_CHUNK_LINES;
        size_t chunk_size;

        if (lines <= 0)
            lines = 1;

        if (bytes_per_line > (size_t)-1 / (size_t)lines)
            return -ENOMEM;

        chunk = (struct scanner_buf_chunk *)malloc(sizeof(*chunk));
        if (!chunk)
            return -ENOMEM;

        chunk_size = bytes_per_line * (size_t)lines;
        chunk->data = (unsigned char *)malloc(chunk_size);
        if (!chunk->data) {
            free(chunk);
            return -ENOMEM;
        }

        chunk->capacity_lines = lines;
        chunk->used_lines = 0;
        chunk->next = NULL;

        if (!cfg->chunk_head)
            cfg->chunk_head = chunk;
        else
            cfg->chunk_tail->next = chunk;
        cfg->chunk_tail = chunk;
    }

    *out_line = chunk->data + (size_t)chunk->used_lines * bytes_per_line;
    chunk->used_lines++;
    return 0;
}

static int scanner_chunk_merge_buff(struct scanner_config *cfg, size_t bytes_per_line, int total_lines)
{
    size_t total_size;
    unsigned char *buf;
    size_t offset = 0;
    struct scanner_buf_chunk *chunk;

    if (!cfg || bytes_per_line == 0 || total_lines <= 0)
        return -EINVAL;

    if (bytes_per_line > (size_t)-1 / (size_t)total_lines)
        return -ENOMEM;

    total_size = bytes_per_line * (size_t)total_lines;

    buf = cfg->scanner_buf;
    if (!buf || cfg->buffer_bytes < total_size) {
        buf = (unsigned char *)malloc(total_size);
        if (!buf)
            return -ENOMEM;
        if (cfg->scanner_buf)
            free(cfg->scanner_buf);
        cfg->scanner_buf = buf;
        cfg->buffer_bytes = total_size;
    }

    chunk = cfg->chunk_head;
    while (chunk) {
        size_t copy_lines = (size_t)chunk->used_lines;
        size_t copy_bytes = copy_lines * bytes_per_line;
        if (offset + copy_bytes > total_size)
            copy_bytes = total_size - offset;

        if (copy_bytes > 0) {
            memcpy(buf + offset, chunk->data, copy_bytes);
            offset += copy_bytes;
        }
        chunk = chunk->next;
    }

    scanner_chunks_reset(cfg);
    cfg->buffer_height = total_lines;
    return 0;
}

/**
 * 1. 滚轮脉冲触发: 中断中 semaphore_post
 *      采集线程 semaphore_wait 等脉冲再采一行
 *      没有滚动就不采集
 * 2. 动态行数
 *      没有固定行数, 滚轮停止超时结束
 *      capture_height =  实际行数
 *
 * 3. chunk 存储
 *      每次采集一行 不直接写进大缓冲
 *      chunk_get_line 把这一行写入当前chunk
 *      chunk 满了再新建下一块
 * 4. 采集完 merge
 *      把所有chunk 合并成一个 scanner_buffer
 *      编码保存连续的buffer
 * 5. 丢弃脉冲积压
 *      避免滚轮停止后还再采集
 */

static int scanner_get_raw_data(struct camera_device *camera,
    const struct camera_info *info, struct scanner_config *cfg)
{
    int line, bpp;
    size_t bytes_per_pixel;
    size_t total_pixels;
    size_t bytes_per_line;
    if (!camera || !info || !cfg)
        return -EINVAL;

    if (!info->width || !info->height) {
        printf("scanner demo: invalid camera size %ux%u\n", info->width, info->height);
        return -EINVAL;
    }

    bytes_per_pixel = scanner_frame_bytes_per_pixel(info);
    if (bytes_per_pixel == 0)
        return -EINVAL;

    total_pixels = info->width * info->height;
    bpp = (cfg->light_mode == SCANNER_LIGHT_RGB) ? 3 : 1;
    bytes_per_line = (size_t)cfg->scanner_width * bpp;

    uint32_t last_pulse_ms = 0;
    scanner_roll_last_ms = 0;
    cfg->capture_height = 0;

    scanner_chunks_reset(cfg);
    for (line = 0; ; ) {
        if (semaphore_wait_timeout(&scanner_roll_sem, SCANNER_ROLL_PULSE_WAIT_SLICE_MS)) {
            uint32_t now_ms = (uint32_t)systick_get_time_ms();
            if (last_pulse_ms &&
                (uint32_t)(now_ms - last_pulse_ms) >= SCANNER_ROLL_PULSE_STOP_TIMEOUT_MS) {
                if (line == 0)
                    return -ETIMEDOUT;
                break;
            }
            continue;
        }
        last_pulse_ms = scanner_roll_last_ms;

        void *frame = camera_wait_frame(camera);
        if (!frame) {
            printf("scanner demo: camera frame error %d\n", camera_get_frame_error(camera));
            return -1;
        }

        unsigned char *current_line = NULL;
        if (scanner_chunk_get_line(cfg, bytes_per_line, &current_line) < 0) {
            camera_put_frame(camera, frame);
            return -ENOMEM;
        }

        if (cfg->light_mode == SCANNER_LIGHT_RGB)
            scanner_rgb_mode(current_line, (unsigned char*)frame, total_pixels);
        else
            scanner_mono_mode(current_line, (unsigned char*)frame, total_pixels, bytes_per_pixel);

        camera_put_frame(camera, frame);

        while (semaphore_try_wait(&scanner_roll_sem) == 0);

        line++;
        if ((line) % 100 == 0)
            printf("Captured: %d lines\n", line);
    }

    cfg->capture_height = line;
    if (cfg->capture_height > 0) {
        if (scanner_chunk_merge_buff(cfg, bytes_per_line, cfg->capture_height) < 0)
            return -ENOMEM;
    }
    return 0;
}

static void scanner_capture_thread(void *data)
{
    struct scanner_capture_ctx *ctx = (struct scanner_capture_ctx *)data;

    if (!ctx)
        thread_delete(NULL);

    int ret = scanner_get_raw_data(ctx->camera, ctx->info, ctx->cfg);
    if (ret < 0) {
        printf("scanner get raw data fail\n");
        thread_delete(NULL);
    }

    semaphore_post(&ctx->done);
}

/*-------------------------------------------------------------------------------*/

/*---------------------------采集校准数据-----------------------------------------*/

/**
 * @brief 可以根据需求修改采集方式: (这里以滚轮采集为例子)
 * @brief 接口描述：
 *        scanner_get_calib_data
 * @param camera camera_device句柄
 * @param info camera_info相关
 * @param cfg scanner_config相关
 * @return
 *         成功返回 >= 0
 *         失败返回 < 0
 */

static int scanner_get_calib_data(struct camera_device *camera,
    const struct camera_info *info, struct scanner_config *cfg)
{
    /** @brief 下面是描述必要的结构框架 需根据伪代码参考修改：
     *  bytes_per_pixel = scanner_frame_bytes_per_pixel(info);
     *  total_pixels = info->width * info->height;
     *  bytes_per_line = (size_t)cfg->scanner_width * bpp;
     *
     *  for (line = 0; line < cfg->capture_height; ) {
            void *frame = camera_wait_frame(camera);
            size_t offset_size = (size_t)line * bytes_per_line;
            unsigned char *current_line = cfg->scanner_buf + offset_size;
            if (cfg->light_mode == SCANNER_LIGHT_RGB)
                scanner_rgb_mode(current_line, (unsigned char*)frame, total_pixels);
            else
                scanner_mono_mode(current_line, (unsigned char*)frame, total_pixels, bytes_per_pixel);
            camera_put_frame(camera, frame);
            line++;
            if ((line) % 100 == 0)
                printf("Captured: %d/%d lines\n", line, cfg->capture_height);
        }
     */
    int line, bpp;
    int skipped = 0;
    size_t bytes_per_pixel;
    size_t total_pixels;
    size_t bytes_per_line;
    uint32_t last_pulse_ms = 0;

    if (!camera || !info || !cfg)
        return -EINVAL;

    if (!info->width || !info->height) {
        printf("scanner demo: invalid camera size %ux%u\n", info->width, info->height);
        return -EINVAL;
    }

    if (cfg->capture_height <= 0)
        return -EINVAL;

    /* 根据输入格式给出每个像素的字节数 */
    bytes_per_pixel = scanner_frame_bytes_per_pixel(info);
    if (bytes_per_pixel == 0)
        return -EINVAL;

    total_pixels = info->width * info->height;
    bpp = ((cfg->light_mode == SCANNER_LIGHT_RGB) ? 3 : 1);

    bytes_per_line = (size_t)cfg->scanner_width * bpp;

    if (!cfg->scanner_buf ||
        cfg->buffer_bytes < (size_t)cfg->capture_height * bytes_per_line)
        return -ENOMEM;

    scanner_roll_last_ms = 0;
    while (semaphore_try_wait(&scanner_roll_sem) == 0); // 避免上一次滚轮残留的脉冲

    for (line = 0; line < cfg->capture_height; ) {
        // 如果超过SCANNER_ROLL_PULSE_STOP_TIMEOUT_MS 时间没有滚动就退出
        if (semaphore_wait_timeout(&scanner_roll_sem, SCANNER_ROLL_PULSE_WAIT_SLICE_MS)) {
            uint32_t now_ms = (uint32_t)systick_get_time_ms();
            if (last_pulse_ms && (now_ms - last_pulse_ms) >= SCANNER_ROLL_PULSE_STOP_TIMEOUT_MS) {
                printf("scanner demo: calib roll timeout at line %d/%d\n",
                       line, cfg->capture_height);
                return -ETIMEDOUT;
            }
            continue;
        }
        last_pulse_ms = scanner_roll_last_ms;

        void *frame = camera_wait_frame(camera);
        if (!frame) {
            printf("scanner demo: wait frame timeout at line %d\n", line);
            return -ETIMEDOUT;
        }

        /* 为保证数据的稳定 先丢掉前SCANNER_SKIP_LINE_COUNT行的数据 */
        if (skipped < SCANNER_SKIP_LINE_COUNT) {
            skipped++;
            camera_put_frame(camera, frame);
            while (semaphore_try_wait(&scanner_roll_sem) == 0);
            continue;
        }

        size_t offset_size = (size_t)line * bytes_per_line;
        if (offset_size + bytes_per_line > cfg->buffer_bytes) {
            camera_put_frame(camera, frame);
            return -ENOMEM;
        }

        unsigned char *current_line = cfg->scanner_buf + offset_size;
        if (cfg->light_mode == SCANNER_LIGHT_RGB)
            scanner_rgb_mode(current_line, (unsigned char*)frame, total_pixels);
        else
            scanner_mono_mode(current_line, (unsigned char*)frame, total_pixels, bytes_per_pixel);

        camera_put_frame(camera, frame);

        while (semaphore_try_wait(&scanner_roll_sem) == 0);
        line++;
        if ((line) % 100 == 0)
            printf("Captured: %d/%d lines\n", line, cfg->capture_height);
    }

    return 0;
}

/*-------------------------------------------------------------------------------*/


/*---------------------------校准算法相关----------------------------------------*/
/**
 * @brief 1. 使用时，直接copy整块“校准算法相关”包含的部分;
 *        2. 只需要根据采集方式不同，修改 “采集校准数据”包含的函数即可;
 */
 struct scanner_calib_data {
    unsigned int magic;
    unsigned short black[3];
    unsigned short gain_q8[3];
    unsigned short led[3];
    unsigned short afe_offset[3];
    unsigned short afe_pga[3];
};

struct scanner_col_calib_header {
    unsigned int magic;
    unsigned int width;
    unsigned int channels;
};

struct scanner_col_sums {
    unsigned int width;
    unsigned int height;
    unsigned int *sum[3];
};

struct scanner_rgb_stats {
    unsigned long long sum[3];
    unsigned int count;
    unsigned char min[3];
    unsigned char max[3];
};

static void scanner_calib_apply(const struct scanner_calib_data *calib)
{
    int i;

    if (!calib)
        return;

    for (i = 0; i < 3; i++) {
        scanner_rgb_black[i] = calib->black[i];
        scanner_rgb_gain_q8[i] = calib->gain_q8[i];
    }
}

static void scanner_calc_rgb_stats(const unsigned char *buf, size_t pixels, struct scanner_rgb_stats *s)
{
    size_t i;

    if (!buf || !s || pixels == 0)
        return;

    s->sum[0] = s->sum[1] = s->sum[2] = 0;
    s->min[0] = s->min[1] = s->min[2] = 255;
    s->max[0] = s->max[1] = s->max[2] = 0;
    s->count = 0;

    for (i = 0; i < pixels; i++) {
        unsigned char r = buf[i * 3 + 0];
        unsigned char g = buf[i * 3 + 1];
        unsigned char b = buf[i * 3 + 2];

        s->sum[0] += r;
        s->sum[1] += g;
        s->sum[2] += b;

        if (r < s->min[0]) s->min[0] = r;
        if (g < s->min[1]) s->min[1] = g;
        if (b < s->min[2]) s->min[2] = b;

        if (r > s->max[0]) s->max[0] = r;
        if (g > s->max[1]) s->max[1] = g;
        if (b > s->max[2]) s->max[2] = b;
    }
    s->count = (unsigned int)pixels;
}

static void scanner_print_rgb_stats(const char *tag, const struct scanner_rgb_stats *s)
{
    unsigned int r = 0, g = 0, b = 0;

    if (!s || s->count == 0)
        return;

    r = (unsigned int)(s->sum[0] / s->count);
    g = (unsigned int)(s->sum[1] / s->count);
    b = (unsigned int)(s->sum[2] / s->count);

    printf("scanner demo: %s mean R/G/B=%u/%u/%u min=%u/%u/%u max=%u/%u/%u\n",
           tag, r, g, b,
           s->min[0], s->min[1], s->min[2],
           s->max[0], s->max[1], s->max[2]);
}

static void scanner_print_rgb_value(const struct scanner_rgb_stats *dark,
                                      const struct scanner_rgb_stats *white)
{
    unsigned int dark_mean[3];
    unsigned int white_mean[3];
    unsigned int delta[3];
    unsigned int gain_q8[3];
    int i;

    if (!dark || !white || dark->count == 0 || white->count == 0)
        return;

    for (i = 0; i < 3; i++) {
        dark_mean[i] = (unsigned int)(dark->sum[i] / dark->count);
        white_mean[i] = (unsigned int)(white->sum[i] / white->count);
        delta[i] = (white_mean[i] > dark_mean[i]) ? (white_mean[i] - dark_mean[i]) : 1;
        gain_q8[i] = (SCANNER_CALIB_TARGET * 256U) / delta[i];
    }

    printf("scanner demo: suggest black R/G/B=%u/%u/%u (mean dark)\n",
           dark_mean[0], dark_mean[1], dark_mean[2]);
    printf("scanner demo: suggest gain  R/G/B=%.2f/%.2f/%.2f (target=%d)\n",
           gain_q8[0] / 256.0, gain_q8[1] / 256.0, gain_q8[2] / 256.0,
           SCANNER_CALIB_TARGET);
}

static int scanner_calc_led_suggest(const struct scanner_rgb_stats *dark,
                                    const struct scanner_rgb_stats *white,
                                    unsigned int rec[3],
                                    unsigned int cur[3],
                                    unsigned int maxv[3])
{
    unsigned int dark_mean[3];
    unsigned int white_mean[3];
    unsigned int delta[3];
    unsigned int delta_min = 0;
    int min_idx = 0;
    int i;

    if (!dark || !white || dark->count == 0 || white->count == 0 || !rec || !cur || !maxv)
        return -EINVAL;

    cur[0] = CONFIG_CIS_DL520_LED_R_BLNESS;
    cur[1] = CONFIG_CIS_DL520_LED_G_BLNESS;
    cur[2] = CONFIG_CIS_DL520_LED_B_BLNESS;
    maxv[0] = CONFIG_CIS_DL520_LED_R_MAX_BLNESS;
    maxv[1] = CONFIG_CIS_DL520_LED_G_MAX_BLNESS;
    maxv[2] = CONFIG_CIS_DL520_LED_B_MAX_BLNESS;

    for (i = 0; i < 3; i++) {
        dark_mean[i] = (unsigned int)(dark->sum[i] / dark->count);
        white_mean[i] = (unsigned int)(white->sum[i] / white->count);
        delta[i] = (white_mean[i] > dark_mean[i]) ? (white_mean[i] - dark_mean[i]) : 1;
    }

    delta_min = delta[0];
    min_idx = 0;
    for (i = 1; i < 3; i++) {
        if (delta[i] < delta_min) {
            delta_min = delta[i];
            min_idx = i;
        }
    }
    if (delta_min == 0)
        delta_min = 1;

    for (i = 0; i < 3; i++) {
        rec[i] = cur[i];
        if (i != min_idx && delta[i] > delta_min) {
            rec[i] = (cur[i] * delta_min + delta[i] / 2) / delta[i];
        }
        if (rec[i] < 1)
            rec[i] = 1;
        if (rec[i] > maxv[i])
            rec[i] = maxv[i];
    }

    return 0;
}

static void scanner_print_led_value(const struct scanner_rgb_stats *dark,
                                      const struct scanner_rgb_stats *white)
{
    unsigned int rec[3];
    unsigned int cur[3];
    unsigned int maxv[3];

    if (scanner_calc_led_suggest(dark, white, rec, cur, maxv) < 0)
        return;

    printf("scanner demo: suggest LED brightness R/G/B=%u/%u/%u (cur=%u/%u/%u, max=%u/%u/%u)\n",
           rec[0], rec[1], rec[2], cur[0], cur[1], cur[2], maxv[0], maxv[1], maxv[2]);
    printf("scanner demo: config hint: CONFIG_CIS_DL520_LED_R_BLNESS=%u, "
           "CONFIG_CIS_DL520_LED_G_BLNESS=%u, CONFIG_CIS_DL520_LED_B_BLNESS=%u\n",
           rec[0], rec[1], rec[2]);
}

static unsigned int scanner_abs_diff_u(unsigned int a, unsigned int b)
{
    return (a > b) ? (a - b) : (b - a);
}

/* 生成校准路径：kind=0(主校准) 1(列校准) 2(MONO列校准) */
static void scanner_calib_make_path(char *buf, size_t size, int dpi, int kind)
{
    const char *fmt = SCANNER_CALIB_PATH_FMT;

    if (!buf || size == 0)
        return;

    if (dpi <= 0)
        dpi = SCANNER_CURRENT_DPI;

    if (kind == 1)
        fmt = SCANNER_CALIB_COL_PATH_FMT;
    else if (kind == 2)
        fmt = SCANNER_CALIB_COL_MONO_PATH_FMT;

    snprintf(buf, size, fmt, dpi);
}

static void scanner_calib_apply_led(struct camera_device *camera,
                                    const struct scanner_calib_data *calib)
{
    int ret;

    if (!camera || !calib)
        return;

    if (calib->led[0] == 0 && calib->led[1] == 0 && calib->led[2] == 0)
        return;

    ret = camera_set_cis_led_brightness(calib->led[0], calib->led[1], calib->led[2]);
    printf("scanner demo: apply led brightness R/G/B=%u/%u/%u ret=%d\n",
           calib->led[0], calib->led[1], calib->led[2], ret);
}

static void scanner_calib_apply_afe(const struct scanner_calib_data *calib)
{
    if (!calib)
        return;

    if (calib->afe_offset[0] || calib->afe_offset[1] || calib->afe_offset[2]) {
        int ret = ht82v38_set_offset(calib->afe_offset[0],
                                     calib->afe_offset[1],
                                     calib->afe_offset[2]);
        printf("scanner demo: apply afe offset R/G/B=%u/%u/%u ret=%d\n",
               calib->afe_offset[0], calib->afe_offset[1], calib->afe_offset[2], ret);
    }

    if (calib->afe_pga[0] || calib->afe_pga[1] || calib->afe_pga[2]) {
        int ret = ht82v38_set_pga(calib->afe_pga[0],
                                  calib->afe_pga[1],
                                  calib->afe_pga[2]);
        printf("scanner demo: apply afe pga R/G/B=%u/%u/%u ret=%d\n",
               calib->afe_pga[0], calib->afe_pga[1], calib->afe_pga[2], ret);
    }
}


/** afe的模拟黑电平偏置
 * 1. 自动计算offset(暗场)
 *  先采暗场（LED off），算 R/G/B 的均值 dm[i]
    取三通道平均 dm_avg
    目标是 SCANNER_AFE_OFFSET_TARGET（现在是 2）
    误差 err = dm_avg - target
    如果偏高（err > tol）：降低 offset
    如果偏低（err < -tol）：提高 offset
    步进 step = ceil(|err| / SCANNER_AFE_OFFSET_STEP_DIV)，再做上下限裁剪
    读取当前 offset（ht82v38_get_offset），算出 next_val
    最后 ht82v38_set_offset(next_val, next_val, next_val)，三通道同值

    2. 应用offset(从校准bin)
        作用在adc前

        软件电平 rgb_black 是数字端偏移 作用在像素值上
 *
 **/

static void scannner_apply_afe_offset(const struct scanner_rgb_stats *dark,
                                             struct scanner_calib_data *calib)
{
    unsigned int dm[3];
    unsigned int dm_avg;
    int err;
    unsigned int step;
    unsigned short cur[3];
    unsigned short next_val;
    int i;
    int ret;

    if (!dark || dark->count == 0 || !calib)
        return;

    ret = ht82v38_get_offset(&cur[0], &cur[1], &cur[2]);
    if (ret < 0)
        return;

    for (i = 0; i < 3; i++)
        dm[i] = (unsigned int)(dark->sum[i] / dark->count);

    dm_avg = (dm[0] + dm[1] + dm[2] + 1) / 3;
    err = (int)dm_avg - (int)SCANNER_AFE_OFFSET_TARGET;

    if (err > SCANNER_AFE_OFFSET_TOL) {
        step = (unsigned int)((err + SCANNER_AFE_OFFSET_STEP_DIV - 1) /
                              SCANNER_AFE_OFFSET_STEP_DIV);
        if (step < 1)
            step = 1;
        if (step > SCANNER_AFE_OFFSET_STEP_MAX)
            step = SCANNER_AFE_OFFSET_STEP_MAX;
        if (cur[1] > (unsigned short)(SCANNER_AFE_OFFSET_MIN + step))
            next_val = (unsigned short)(cur[1] - step);
        else
            next_val = SCANNER_AFE_OFFSET_MIN;
    } else if (err < -SCANNER_AFE_OFFSET_TOL) {
        step = (unsigned int)((-err + SCANNER_AFE_OFFSET_STEP_DIV - 1) /
                              SCANNER_AFE_OFFSET_STEP_DIV);
        if (step < 1)
            step = 1;
        if (step > SCANNER_AFE_OFFSET_STEP_MAX)
            step = SCANNER_AFE_OFFSET_STEP_MAX;
        next_val = (unsigned short)(cur[1] + step);
        if (next_val > SCANNER_AFE_OFFSET_MAX)
            next_val = SCANNER_AFE_OFFSET_MAX;
    } else {
        next_val = cur[1];
    }

    ret = ht82v38_set_offset(next_val, next_val, next_val);
    printf("scanner demo: set afe offset R/G/B=%u/%u/%u (cur=%u/%u/%u, dark_avg=%u) ret=%d\n",
           next_val, next_val, next_val, cur[0], cur[1], cur[2], dm_avg, ret);

    calib->afe_offset[0] = next_val;
    calib->afe_offset[1] = next_val;
    calib->afe_offset[2] = next_val;
}


/** 调节pga应用亮度
 * 1. 先计算当前白场/黑场差值 delta_avg
 * 2. 按照目标亮度 TARIGET 计算出一个比例
 *    ratio_q12 = (target << 12) / delta_avg
 *
 * 通过宏把比例调节到一个范围内:
 *    避免过大或过于小
 */

static void scannner_apply_afe_pga(const struct scanner_rgb_stats *dark,
                                          const struct scanner_rgb_stats *white,
                                          struct scanner_calib_data *calib)
{
    unsigned int dm[3];
    unsigned int wm[3];
    unsigned int delta[3];
    unsigned int delta_avg;
    unsigned int ratio_q12;
    unsigned short cur[3];
    unsigned short next_val;
    int i;
    int ret;

    if (!dark || !white || dark->count == 0 || white->count == 0 || !calib)
        return;

    ret = ht82v38_get_pga(&cur[0], &cur[1], &cur[2]);
    if (ret < 0)
        return;

    for (i = 0; i < 3; i++) {
        dm[i] = (unsigned int)(dark->sum[i] / dark->count);
        wm[i] = (unsigned int)(white->sum[i] / white->count);
        delta[i] = (wm[i] > dm[i]) ? (wm[i] - dm[i]) : 1;
    }

    delta_avg = (delta[0] + delta[1] + delta[2] + 1) / 3;
    if (delta_avg == 0)
        return;

    ratio_q12 = ((unsigned int)SCANNER_CALIB_TARGET << 12) / delta_avg;
    if (ratio_q12 < SCANNER_AFE_PGA_RATIO_MIN_Q12)
        ratio_q12 = SCANNER_AFE_PGA_RATIO_MIN_Q12;
    if (ratio_q12 > SCANNER_AFE_PGA_RATIO_MAX_Q12)
        ratio_q12 = SCANNER_AFE_PGA_RATIO_MAX_Q12;

    next_val = (unsigned short)((cur[1] * ratio_q12 + 2048) >> 12);
    if (next_val < SCANNER_AFE_PGA_MIN)
        next_val = SCANNER_AFE_PGA_MIN;
    if (next_val > SCANNER_AFE_PGA_MAX)
        next_val = SCANNER_AFE_PGA_MAX;

    ret = ht82v38_set_pga(next_val, next_val, next_val);
    printf("scanner demo: set afe pga R/G/B=%u/%u/%u (cur=%u/%u/%u) ret=%d\n",
           next_val, next_val, next_val, cur[0], cur[1], cur[2], ret);

    calib->afe_pga[0] = next_val;
    calib->afe_pga[1] = next_val;
    calib->afe_pga[2] = next_val;
}

static int scanner_calib_load(const char *path, struct scanner_calib_data *calib)
{
    int ret;

    if (!calib || !path)
        return -1;

    ret = scanner_read_file(path, calib, sizeof(*calib));
    if (ret != (int)sizeof(*calib))
        return -1;

    if (calib->magic != SCANNER_CALIB_MAGIC)
        return -1;

    return 0;
}

static int scanner_calib_save(const char *path, const struct scanner_calib_data *calib)
{
    if (!calib || !path)
        return -1;

    return scanner_write_file(path, calib, sizeof(*calib));
}

static void scanner_col_sums_free(struct scanner_col_sums *s)
{
    int i;

    if (!s)
        return;

    for (i = 0; i < 3; i++) {
        free(s->sum[i]);
        s->sum[i] = NULL;
    }
    s->width = 0;
    s->height = 0;
}

static int scanner_col_sums_alloc(struct scanner_col_sums *s, unsigned int width, unsigned int height)
{
    int i;

    if (!s || width == 0 || height == 0)
        return -EINVAL;

    memset(s, 0, sizeof(*s));
    s->width = width;
    s->height = height;

    for (i = 0; i < 3; i++) {
        s->sum[i] = (unsigned int *)malloc(width * sizeof(unsigned int));
        if (!s->sum[i]) {
            scanner_col_sums_free(s);
            return -ENOMEM;
        }
        memset(s->sum[i], 0, width * sizeof(unsigned int));
    }

    return 0;
}

static int scanner_col_sums_accum(const unsigned char *buf, unsigned int width,
    unsigned int height, struct scanner_col_sums *s)
{
    unsigned int y;

    if (!buf || !s || width == 0 || height == 0)
        return -EINVAL;

    if (s->width != width || s->height != height)
        return -EINVAL;

    for (y = 0; y < height; y++) {
        const unsigned char *row = buf + (size_t)y * width * 3;
        unsigned int x;

        for (x = 0; x < width; x++) {
            size_t off = (size_t)x * 3;
            s->sum[0][x] += row[off + 0];
            s->sum[1][x] += row[off + 1];
            s->sum[2][x] += row[off + 2];
        }
    }

    return 0;
}

static int scanner_col_sums_accum_mono(const unsigned char *buf, unsigned int width,
    unsigned int height, struct scanner_col_sums *s)
{
    unsigned int y;

    if (!buf || !s || width == 0 || height == 0)
        return -EINVAL;

    if (s->width != width || s->height != height)
        return -EINVAL;

    for (y = 0; y < height; y++) {
        const unsigned char *row = buf + (size_t)y * width;
        unsigned int x;

        for (x = 0; x < width; x++) {
            unsigned int v = row[x];
            s->sum[0][x] += v;
            s->sum[1][x] += v;
            s->sum[2][x] += v;
        }
    }

    return 0;
}

static void scanner_col_calib_clear(void)
{
    int i;

    for (i = 0; i < 3; i++) {
        free(scanner_col_black[i]);
        free(scanner_col_gain_q8[i]);
        scanner_col_black[i] = NULL;
        scanner_col_gain_q8[i] = NULL;
    }
    scanner_col_width = 0;
    scanner_col_valid = 0;
}

static int scanner_col_calib_alloc(unsigned int width)
{
    int i;

    if (width == 0)
        return -EINVAL;

    scanner_col_calib_clear();
    for (i = 0; i < 3; i++) {
        scanner_col_black[i] = (unsigned short *)malloc(width * sizeof(unsigned short));
        scanner_col_gain_q8[i] = (unsigned short *)malloc(width * sizeof(unsigned short));
        if (!scanner_col_black[i] || !scanner_col_gain_q8[i]) {
            scanner_col_calib_clear();
            return -ENOMEM;
        }
    }

    scanner_col_width = width;
    scanner_col_valid = 0;
    return 0;
}

static int scanner_col_calib_build(const struct scanner_col_sums *dark,
    const struct scanner_col_sums *white)
{
    unsigned int x;
    int ch;

    if (!dark || !white || dark->width != white->width || dark->height != white->height)
        return -EINVAL;

    if (scanner_col_calib_alloc(dark->width) < 0)
        return -ENOMEM;

    for (x = 0; x < dark->width; x++) {
        for (ch = 0; ch < 3; ch++) {
            unsigned int d = dark->sum[ch][x] / dark->height;
            unsigned int w = white->sum[ch][x] / white->height;
            unsigned int delta = (w > d) ? (w - d) : 1;
            unsigned int gain_q8 = (SCANNER_CALIB_TARGET * 256U) / delta;

            if (gain_q8 < 16)
                gain_q8 = 16;
            if (gain_q8 > 1024)
                gain_q8 = 1024;

            scanner_col_black[ch][x] = (unsigned short)d;
            scanner_col_gain_q8[ch][x] = (unsigned short)gain_q8;
        }
    }

    scanner_col_valid = 1;
    return 0;
}


static int scanner_col_calib_load(const char *path, unsigned int expect_width)
{
    int fd;
    struct scanner_col_calib_header hdr;
    size_t bytes;
    int i;

    if (!path)
        return -1;

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return -1;

    if (scanner_read_all(fd, &hdr, sizeof(hdr)) < 0) {
        close(fd);
        return -1;
    }

    if (hdr.magic != SCANNER_COL_CALIB_MAGIC) {
        close(fd);
        return -1;
    }

    if (hdr.channels != 3 || hdr.width == 0 || (expect_width && hdr.width != expect_width)) {
        close(fd);
        return -1;
    }

    if (scanner_col_calib_alloc(hdr.width) < 0) {
        close(fd);
        return -1;
    }

    bytes = (size_t)hdr.width * sizeof(unsigned short);
    for (i = 0; i < 3; i++) {
        if (scanner_read_all(fd, scanner_col_black[i], bytes) < 0)
            goto err;
    }
    for (i = 0; i < 3; i++) {
        if (scanner_read_all(fd, scanner_col_gain_q8[i], bytes) < 0)
            goto err;
    }

    close(fd);
    scanner_col_valid = 1;
    return 0;

err:
    close(fd);
    scanner_col_calib_clear();
    return -1;
}

static int scanner_col_calib_save(const char *path, unsigned int width)
{
    struct scanner_col_calib_header hdr;
    unsigned char *buf;
    unsigned char *ptr;
    size_t bytes;
    size_t total;
    int i;
    int ret;

    if (!path || !scanner_col_valid || width == 0 || width != scanner_col_width)
        return -1;

    hdr.magic = SCANNER_COL_CALIB_MAGIC;
    hdr.width = width;
    hdr.channels = 3;

    bytes = (size_t)width * sizeof(unsigned short);
    total = sizeof(hdr) + bytes * 6;
    buf = (unsigned char *)malloc(total);
    if (!buf)
        return -ENOMEM;

    ptr = buf;
    memcpy(ptr, &hdr, sizeof(hdr));
    ptr += sizeof(hdr);
    for (i = 0; i < 3; i++) {
        memcpy(ptr, scanner_col_black[i], bytes);
        ptr += bytes;
    }
    for (i = 0; i < 3; i++) {
        memcpy(ptr, scanner_col_gain_q8[i], bytes);
        ptr += bytes;
    }

    ret = scanner_write_file(path, buf, total);
    free(buf);
    return ret;
}

static int scanner_calib_capture(struct camera_device *camera,
    const struct camera_info *info, struct scanner_config *cfg,
    int led_on, const char *tag, struct scanner_rgb_stats *stats,
    int *is_stream_on)
{
    int ret;

    if (!camera || !info || !cfg)
        return -EINVAL;

    if (is_stream_on && *is_stream_on) {
        camera_stream_off(camera);
        *is_stream_on = 0;
        mdelay(20);
    }

    ret = camera_set_cis_led_enable(led_on);
    printf("scanner demo: calib %s, set led %s ret=%d\n", tag, led_on ? "on" : "off", ret);
    mdelay(10);

    ret = camera_stream_on(camera);
    printf("scanner demo: calib %s, stream on ret=%d\n", tag, ret);

    if (ret < 0)
        return ret;
    if (is_stream_on)
        *is_stream_on = 1;

    ret = scanner_get_calib_data(camera, info, cfg);

    camera_stream_off(camera);
    if (is_stream_on)
        *is_stream_on = 0;

    mdelay(10);

    if (ret < 0)
        return ret;

    if (stats && cfg->light_mode == SCANNER_LIGHT_RGB) {
        size_t pixels = (size_t)cfg->scanner_width * (size_t)cfg->capture_height;
        scanner_calc_rgb_stats(cfg->scanner_buf, pixels, stats);
        scanner_print_rgb_stats(tag, stats);
    }

    return 0;
}

static int scanner_apply_led_stepwise(struct camera_device *camera,
                                      const struct camera_info *info,
                                      struct scanner_config *cfg,
                                      const struct scanner_rgb_stats *dark,
                                      const struct scanner_rgb_stats *white,
                                      struct scanner_calib_data *calib,
                                      int *is_stream_on)
{
    unsigned int cur[3];
    unsigned int maxv[3];
    unsigned int white_mean[3];
    unsigned int ref_min;
    int min_idx;
    unsigned int step;
    int i;

    if (!camera || !info || !cfg || !dark || !white || !calib)
        return -EINVAL;
    if (dark->count == 0 || white->count == 0)
        return -EINVAL;

    // 当前亮度值
    cur[0] = CONFIG_CIS_DL520_LED_R_BLNESS;
    cur[1] = CONFIG_CIS_DL520_LED_G_BLNESS;
    cur[2] = CONFIG_CIS_DL520_LED_B_BLNESS;
    // 最大亮度值
    maxv[0] = CONFIG_CIS_DL520_LED_R_MAX_BLNESS;
    maxv[1] = CONFIG_CIS_DL520_LED_G_MAX_BLNESS;
    maxv[2] = CONFIG_CIS_DL520_LED_B_MAX_BLNESS;

    // 求白场平均值
    white_mean[0] = white_mean[1] = white_mean[2] = 0;
    if (white && white->count) {
        white_mean[0] = (unsigned int)(white->sum[0] / white->count);
        white_mean[1] = (unsigned int)(white->sum[1] / white->count);
        white_mean[2] = (unsigned int)(white->sum[2] / white->count);
    }

    // 找到最暗通道
    min_idx = 0;
    ref_min = white_mean[0];
    for (i = 1; i < 3; i++) {
        if (white_mean[i] < ref_min) {
            ref_min = white_mean[i];
            min_idx = i;
        }
    }
    if (ref_min == 0)
        ref_min = 1;

    if (cfg->dpi >= 1200)
        step = SCANNER_LED_STEP_H;
    else if (cfg->dpi >= 600)
        step = SCANNER_LED_STEP_M;
    else
        step = SCANNER_LED_STEP_L;
    if (step == 0)
        step = 1;

    // 根据不同dpi的步长进行减亮 靠近最暗通道 只减不加
    for (i = 0; i < 3; i++) {
        int iter;
        unsigned int prev_mean;

        if (i == min_idx)
            continue;

        prev_mean = white_mean[i];
        for (iter = 0; iter < SCANNER_LED_STEP_MAX_STEPS; iter++) {
            struct scanner_rgb_stats tmp_white;
            unsigned int tmp_mean[3];
            int tmp_height;
            int ret;

            if (cur[i] <= step)
                break;

            cur[i] -= step;
            if (cur[i] < 1)
                cur[i] = 1;
            if (cur[i] > maxv[i])
                cur[i] = maxv[i];

            ret = camera_set_cis_led_brightness(cur[0], cur[1], cur[2]);
            if (ret < 0)
                return ret;

            tmp_height = cfg->capture_height;
            if (SCANNER_LED_CALIB_HEIGHT > 0)
                cfg->capture_height = SCANNER_LED_CALIB_HEIGHT;

            // 重新抓一帧白场 比对均值是否接近最暗通道
            ret = scanner_calib_capture(camera, info, cfg, 1, "white_led", &tmp_white, is_stream_on);
            cfg->capture_height = (int)tmp_height;
            if (ret < 0)
                return ret;

            tmp_mean[0] = tmp_mean[1] = tmp_mean[2] = 0;
            if (tmp_white.count) {
                tmp_mean[0] = (unsigned int)(tmp_white.sum[0] / tmp_white.count);
                tmp_mean[1] = (unsigned int)(tmp_white.sum[1] / tmp_white.count);
                tmp_mean[2] = (unsigned int)(tmp_white.sum[2] / tmp_white.count);
            }

            // 接近最暗通道就继续减 否则退出停止
            if (scanner_abs_diff_u(tmp_mean[i], ref_min) <
                scanner_abs_diff_u(prev_mean, ref_min)) {
                prev_mean = tmp_mean[i];
                continue;
            }

            cur[i] += step;
            if (cur[i] > maxv[i])
                cur[i] = maxv[i];
            camera_set_cis_led_brightness(cur[0], cur[1], cur[2]);
            break;
        }
    }

    calib->led[0] = (unsigned short)cur[0];
    calib->led[1] = (unsigned short)cur[1];
    calib->led[2] = (unsigned short)cur[2];
    printf("scanner demo: led step result R/G/B=%u/%u/%u (step=%u)\n",
           cur[0], cur[1], cur[2], step);
    return 0;
}

/**
 * @brief scanner自动校准算法
 *
 * @param 黑场采集  1.关灯采集, 统计均值, 用于解决raw图存在蒙层情况;
 *                  2.根据暗场均值调用afe offset, 拉准模拟端黑电平,避免后续数字补偿拉偏;
 *
 * @param 白场采集  1. 开灯采集, 统计均值, 用于判断三色亮度差,整体亮度差,
 *                 为led和gain提供参考,避免偏色和亮度不均;
 *
 * @param led亮度平衡  1. 找到最暗通道,逐步降低其他通道亮度,使rgb困值对齐, 用于解决整体偏色问题;
 *
 * @param led调整后黑场白场  1. 更新白场均值用于afe gain 避免旧白场导致亮度偏移;
 *                           2. afe gain模拟增益,根据黑白场计算调整比例, 用于解决亮度过爆,噪声偏大问题;
 *
 * @param 列校准  1. 经过led和afe调整后,重新采集黑/白场,用于统计真实响应, 用于解决图像存在黑色条纹现象;
 *
 * mono 模式校准 只做列校准 消除黑线
 *
 * 1. 有校准bin时:
 *      a. 根据bin中的参数进行应用校准
 *
 * 2. 没有校准bin时:
 *      a. dark（关灯暗场):
 *          计算暗场均值，用于 AFE offset 估算.
 *      b. white（开灯白场):
 *          计算白场均值，用于 LED 亮度和 PGA 增益估算.
 *      c. LED step 校准:
 *          找到最暗通道做参考，逐步降低其它通道亮度，使 RGB 靠齐.
 *      d. white2:
 *          LED 调整后再抓白场.
 *      e. AFE PGA 校准:
 *          用白/黑差值算比例，给 AFE PGA.
 *      f. dark2 & white3:
 *          用于最终软件黑电平和列校准的统计.
 *      g. 生成并保存:
 *          calib_save 保存黑电平/增益/led/afe.
 *          col_calib_save 保存列校准.
 *      h. 校准抓图:
 *          前20行丢弃 预热 避免切换不稳定.
 */

static int scanner_calibrate_flow(struct camera_device *camera,
                                   const struct camera_info *info,
                                   struct scanner_config *cfg,
                                   int *is_stream_on)
{
    int ret;
    int tmp_height;
    int col_loaded = 1;
    int calib_loaded = 0;
    int dark_col_ok = 0;
    int white_col_ok = 0;

    char col_path[64];
    char calib_path[64];

    struct scanner_calib_data calib;
    struct scanner_col_sums dark_col;
    struct scanner_col_sums white_col;
    struct scanner_rgb_stats dark_stats;
    struct scanner_rgb_stats white_stats;

    if (!camera || !info || !cfg)
        return -EINVAL;

    if (cfg->light_mode != SCANNER_LIGHT_RGB) {
        scanner_calib_make_path(col_path, sizeof(col_path), cfg->dpi, 2); // 映射路径
        scanner_col_calib_clear(); // 清除现有的列校准数据, 避免串扰

        if (scanner_col_calib_load(col_path, cfg->scanner_width) == 0) {
            printf("scanner demo: mono col calib loaded from %s (width=%u)\n",
                   col_path, scanner_col_width);
            return 0;
        }

        // 设置中性参数, 确保采集前, 数据不偏差(真实反映)
        scanner_rgb_black[0] = 0;
        scanner_rgb_black[1] = 0;
        scanner_rgb_black[2] = 0;
        scanner_rgb_gain_q8[0] = 1 << SCANNER_RGB_GAIN_SHIFT;
        scanner_rgb_gain_q8[1] = 1 << SCANNER_RGB_GAIN_SHIFT;
        scanner_rgb_gain_q8[2] = 1 << SCANNER_RGB_GAIN_SHIFT;
        scanner_col_valid = 0;

        tmp_height = cfg->capture_height;
        cfg->capture_height = SCANNER_CALIB_HEIGHT;

        // 黑场采集
        ret = scanner_calib_capture(camera, info, cfg, 0, "mono_dark", NULL, is_stream_on);
        if (ret < 0)
            goto out_mono;

        if (scanner_col_sums_alloc(&dark_col, cfg->scanner_width, cfg->capture_height) == 0) {
            if (scanner_col_sums_accum_mono(cfg->scanner_buf, cfg->scanner_width,
                                            cfg->capture_height, &dark_col) == 0)
                dark_col_ok = 1;
            else
                scanner_col_sums_free(&dark_col);
        }

        // 白场采集
        ret = scanner_calib_capture(camera, info, cfg, 1, "mono_white", NULL, is_stream_on);
        if (ret < 0)
            goto out_mono;

        if (scanner_col_sums_alloc(&white_col, cfg->scanner_width, cfg->capture_height) == 0) {
            if (scanner_col_sums_accum_mono(cfg->scanner_buf, cfg->scanner_width,
                                            cfg->capture_height, &white_col) == 0)
                white_col_ok = 1;
            else
                scanner_col_sums_free(&white_col);
        }

        // 存储校准数据
        if (dark_col_ok && white_col_ok) {
            if (scanner_col_calib_build(&dark_col, &white_col) == 0) {
                if (scanner_col_calib_save(col_path, cfg->scanner_width) == 0)
                    printf("scanner demo: mono col calib saved to %s\n", col_path);
            }
        }

out_mono:
        if (dark_col_ok)
            scanner_col_sums_free(&dark_col);
        if (white_col_ok)
            scanner_col_sums_free(&white_col);

        cfg->capture_height = tmp_height;
        return ret;
    }

    col_loaded = 0;

    // rgb calibrate
    scanner_calib_make_path(calib_path, sizeof(calib_path), cfg->dpi, 0); // 映射路径
    scanner_calib_make_path(col_path, sizeof(col_path), cfg->dpi, 1);

    if (scanner_calib_load(calib_path, &calib) == 0) {
        scanner_calib_apply(&calib); // 加载黑白场统计值
        scanner_calib_apply_led(camera, &calib); // 应用led brighteness的校准值
        scanner_calib_apply_afe(&calib);    // 设置afe的offset和pga
        calib_loaded = 1;

        printf("scanner demo: calib loaded from %s\n", calib_path);
        printf("scanner demo: calib black R/G/B=%u/%u/%u gain=%.2f/%.2f/%.2f\n",
                    calib.black[0], calib.black[1], calib.black[2],
                    calib.gain_q8[0] / 256.0, calib.gain_q8[1] / 256.0, calib.gain_q8[2] / 256.0);

        printf("scanner demo: calib led R/G/B=%u/%u/%u\n", calib.led[0], calib.led[1], calib.led[2]);
        printf("scanner demo: calib afe offset R/G/B=%u/%u/%u\n", calib.afe_offset[0], calib.afe_offset[1], calib.afe_offset[2]);
        printf("scanner demo: calib afe pga R/G/B=%u/%u/%u\n", calib.afe_pga[0], calib.afe_pga[1], calib.afe_pga[2]);
    }

    scanner_col_calib_clear();
    if (scanner_col_calib_load(col_path, cfg->scanner_width) == 0) {
        col_loaded = 1;
        printf("scanner demo: col calib loaded from %s (width=%u)\n", col_path, scanner_col_width);
    }

    if (calib_loaded && col_loaded)
        return 0;

    // 设置中性参数, 确保采集前, 数据不偏差(真实反映)
    scanner_rgb_black[0] = 0;
    scanner_rgb_black[1] = 0;
    scanner_rgb_black[2] = 0;
    scanner_rgb_gain_q8[0] = 1 << SCANNER_RGB_GAIN_SHIFT;
    scanner_rgb_gain_q8[1] = 1 << SCANNER_RGB_GAIN_SHIFT;
    scanner_rgb_gain_q8[2] = 1 << SCANNER_RGB_GAIN_SHIFT;

    tmp_height = cfg->capture_height;
    cfg->capture_height = SCANNER_CALIB_HEIGHT;

    // 黑场采集
    ret = scanner_calib_capture(camera, info, cfg, 0, "dark", &dark_stats, is_stream_on);
    if (ret < 0)
        goto out_restore;

    // 从暗场获取数据 并应用
    calib.afe_offset[0] = calib.afe_offset[1] = calib.afe_offset[2] = 0;
    scannner_apply_afe_offset(&dark_stats, &calib);

    // 白场采集
    ret = scanner_calib_capture(camera, info, cfg, 1, "white", &white_stats, is_stream_on);
    if (ret < 0)
        goto out_restore;

    // dump
    scanner_print_rgb_value(&dark_stats, &white_stats);
    scanner_print_led_value(&dark_stats, &white_stats);

    // led根据步进设置亮度
    calib.led[0] = calib.led[1] = calib.led[2] = 0;
    ret = scanner_apply_led_stepwise(camera, info, cfg, &dark_stats, &white_stats, &calib, is_stream_on);
    if (ret < 0) {
        printf("scanner demo: led stepwise failed, skip led adjust\n");
        calib.led[0] = calib.led[1] = calib.led[2] = 0;
    }

    // led调节完成后 重新进行白场采集
    ret = scanner_calib_capture(camera, info, cfg, 1, "white2", &white_stats, is_stream_on);
    if (ret < 0)
        goto out_restore;

    // 根据白场统计值进行设置afe的gain 增益
    calib.afe_pga[0] = calib.afe_pga[1] = calib.afe_pga[2] = 0;
    scannner_apply_afe_pga(&dark_stats, &white_stats, &calib);

    // 进行dark2 和 white3 采集, 用于最终软件黑电平和列校准统计
    ret = scanner_calib_capture(camera, info, cfg, 0, "dark2", &dark_stats, is_stream_on);
    if (ret < 0)
        goto out_restore;

    if (scanner_col_sums_alloc(&dark_col, cfg->scanner_width, cfg->capture_height) == 0) {
        if (scanner_col_sums_accum(cfg->scanner_buf, cfg->scanner_width,
                                   cfg->capture_height, &dark_col) == 0)
            dark_col_ok = 1;
        else
            scanner_col_sums_free(&dark_col);
    }

    // white3 采集
    ret = scanner_calib_capture(camera, info, cfg, 1, "white3", &white_stats, is_stream_on);
    if (ret < 0)
        goto out_restore;

    if (scanner_col_sums_alloc(&white_col, cfg->scanner_width, cfg->capture_height) == 0) {
        if (scanner_col_sums_accum(cfg->scanner_buf, cfg->scanner_width,
                                   cfg->capture_height, &white_col) == 0)
            white_col_ok = 1;
        else
            scanner_col_sums_free(&white_col);
    }

    // 保存校准数据
    calib.magic = SCANNER_CALIB_MAGIC;
    calib.black[0] = (unsigned short)(dark_stats.sum[0] / dark_stats.count);
    calib.black[1] = (unsigned short)(dark_stats.sum[1] / dark_stats.count);
    calib.black[2] = (unsigned short)(dark_stats.sum[2] / dark_stats.count);

    unsigned int dm[3];
    unsigned int wm[3];
    unsigned int delta[3];
    unsigned int gain_q8[3];
    int i;

    for (i = 0; i < 3; i++) {
        dm[i] = (unsigned int)(dark_stats.sum[i] / dark_stats.count);
        wm[i] = (unsigned int)(white_stats.sum[i] / white_stats.count);
        delta[i] = (wm[i] > dm[i]) ? (wm[i] - dm[i]) : 1;
        gain_q8[i] = (SCANNER_CALIB_TARGET * 256U) / delta[i];
        if (gain_q8[i] < 16)
            gain_q8[i] = 16;
        if (gain_q8[i] > 1024)
            gain_q8[i] = 1024;
        calib.gain_q8[i] = (unsigned short)gain_q8[i];
    }

    scanner_calib_apply(&calib);
    if (scanner_calib_save(calib_path, &calib) == 0)
        printf("scanner demo: calib saved to %s\n", calib_path);

    if (dark_col_ok && white_col_ok) {
        if (scanner_col_calib_build(&dark_col, &white_col) == 0) {
            if (scanner_col_calib_save(col_path, cfg->scanner_width) == 0)
                printf("scanner demo: col calib saved to %s\n", col_path);
        }
    }

    if (dark_col_ok)
        scanner_col_sums_free(&dark_col);
    if (white_col_ok)
        scanner_col_sums_free(&white_col);

    cfg->capture_height = tmp_height;
    return 0;

out_restore:
    if (dark_col_ok)
        scanner_col_sums_free(&dark_col);
    if (white_col_ok)
        scanner_col_sums_free(&white_col);
    cfg->capture_height = tmp_height;
    return ret;
}
/*-------------------------------------------------------------------------------*/

void scanner_dump_dpi(struct camera_device *camera)
{
    int i;
    int ret;
    int current_dpi = -1;
    int usable_count = 0;
    struct cis_dpi_config *support_dpi;
    struct cis_dpi_config cur_dpi = {0};

    /* 获取当前注册进驱动的cis dpi */
    ret = camera_get_cis_dpi(camera, &cur_dpi);
    if (ret < 0) {
        printf("get cis dpi failed\n");
        return;
    }

    current_dpi = cur_dpi.dpi;
    printf("get current register dpi:%d\n", current_dpi);

    /* 获取当前支持的所有cis信息：dpi tr_cycle pixel_cycle */
    support_dpi = camera_get_support_dpi(camera, &usable_count);
    for (i = 0; i < usable_count; i++) {
        printf("support: dpi:%d tr_cycle:%d pixel:%d\n", support_dpi[i].dpi, support_dpi[i].tr_cycle, support_dpi[i].pixel_cycle);
    }
}

void pwm_scanner_test(void)
{
    int ret;
    int pwm_id = -1;

   struct camera_device *camera = NULL;
   struct camera_info *cam_info = NULL;

    struct scanner_config cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.capture_height = 0;
    cfg.buffer_height = SCANNER_CALIB_HEIGHT;
    cfg.light_mode = SCANNER_LIGHT_MODE;
    cfg.format = SCANNER_OUTPUT_FORMAT;
    cfg.channels = SCANNER_CHANNELS;
    cfg.dpi = SCANNER_CURRENT_DPI;

    int bytes;
    size_t buffer_size;
    static int is_stream_on = 0;

    camera = scanner_init(cam_info);
    if (!camera) {
        printf("scanner_init fail\n");
        return;
    }

    /* 打印当前支持的dpi */
    scanner_dump_dpi(camera);

    if (is_stream_on) {
        camera_stream_off(camera);
        is_stream_on = 0;
    }

    /* 设置dpi */
    ret = camera_set_cis_dpi(camera, cfg.dpi);
    if (ret < 0) {
        printf("set cis dpi failed\n");
        goto out_pwm;
    }
    printf ("switch dpi to %d currently\n", cfg.dpi);

    cam_info = camera_get_info(camera);
    assert(cam_info);

    /* 内存申请 */
    switch (cam_info->data_fmt) {
        case CAMERA_PIX_FMT_GREY:
            bytes = 1;
            break;
        case CAMERA_PIX_FMT_RGB565:
            bytes = 2;
            break;
        case CAMERA_PIX_FMT_RGB24:
        case CAMERA_PIX_FMT_RBG24:
        case CAMERA_PIX_FMT_GBR24:
        case CAMERA_PIX_FMT_GRB24:
        case CAMERA_PIX_FMT_BGR24:
            bytes = 3;
            break;
        default:
            printf("scanner demo: unsupported fmt %c%c%c%c\n",
                   cam_info->data_fmt,
                   cam_info->data_fmt >> 8,
                   cam_info->data_fmt >> 16,
                   cam_info->data_fmt >> 24);
            goto out_pwm;
    }

    cfg.scanner_width = cam_info->width * cam_info->height;

    /* 滚轮中断初始化 */
    scanner_roll_irq_init();

    buffer_size = cfg.scanner_width * cfg.buffer_height * bytes;

    cfg.scanner_buf = (unsigned char *)malloc(buffer_size);
    if (!cfg.scanner_buf)
        goto out_pwm;
    cfg.buffer_bytes = buffer_size;

    /* 自动校准flow */
    ret = scanner_calibrate_flow(camera, cam_info, &cfg, &is_stream_on);
    if (ret < 0)
        goto out_pwm;

    /* 存在黑场, led会被disable, 而采集时灯光一定是亮起状态 (故需set led enable做保证) */
    ret = camera_set_cis_led_enable(1);
    if (ret < 0)
        printf("turn on led fail!\n");

    /* 开流 */
    ret = camera_stream_on(camera);
    if (ret < 0)
        goto out_pwm;
    is_stream_on = 1;

    /* 裸图数据: 脉冲驱动采集线程 */
    struct scanner_capture_ctx cap;
    memset(&cap, 0, sizeof(cap));
    cap.camera = camera;
    cap.info = cam_info;
    cap.cfg = &cfg;
    semaphore_init(&cap.done, 0);

    thread_ptr_t cap_thread = thread_create("scanner_cap", 8192, scanner_capture_thread, &cap);
    if (!cap_thread) {
        printf("scanner demo: create capture thread failed\n");
        goto out_pwm;
    }

    semaphore_wait(&cap.done);
    if (is_stream_on) {
        camera_stream_off(camera);
        if (ret < 0)
            goto out_pwm;

        is_stream_on = 0;
    }

    struct scanner_jpeg_output jpeg = {0};

    /* 编码 */
    if (cfg.format == SCANNER_FORMAT_JPG) {
        ret = scanner_encode_jpeg(&cfg, &jpeg);
        if (ret < 0)
            goto out_encode;

    /* 保存 */
    #ifdef CONFIG_DFS
        if (scanner_write_file(SCANNER_OUTPUT_JPG_PATH, jpeg.out->data, jpeg.out->data_size) < 0)
            ret = -1;
        else
            printf("scanner demo: jpeg saved to %s (%u bytes)\n",
                   SCANNER_OUTPUT_JPG_PATH, jpeg.out->data_size);
    #else
        printf("scanner demo: jpeg size %zu bytes\n", jpeg.out->data_size);
    #endif

    /* 显示 */
    #if SCANNER_DISPLAY_ON_FB
    if (ret == 0) {
        struct jpegd_decoder *decoder = NULL;
        struct jpegd_decoder_output *out = NULL;

        if (scanner_decode_jpeg(jpeg.out->data, jpeg.out->data_size,
                                jpeg.out->width, jpeg.out->height,
                                &decoder, &out) < 0) {
            printf("scanner demo: jpeg decode failed\n");
        } else {
            if (scanner_display_decoded_to_fb(out) < 0)
                printf("scanner demo: jpeg display failed\n");

            scanner_free_decoded_jpeg(decoder, out);
        }
    }
    #endif
    } else {
        size_t out_size = (size_t)cfg.scanner_width * (size_t)cfg.capture_height *
                          ((cfg.light_mode == SCANNER_LIGHT_RGB) ? 3 : 1);

    /* raw直接保存 */
    #ifdef CONFIG_DFS
        if (scanner_write_file(SCANNER_OUTPUT_RAW_PATH, cfg.scanner_buf, out_size) < 0)
            ret = -1;
        else
            printf("scanner demo: raw saved to %s (%zu bytes)\n",
                SCANNER_OUTPUT_RAW_PATH, out_size);
    #else
        printf("scanner demo: raw size %zu bytes\n", out_size);
    #endif
    }

out_encode:
    scanner_free_jpeg_output(&jpeg);

out_pwm:
    if (is_stream_on) {
        camera_stream_off(camera);
        is_stream_on = 0;
    }
    camera_power_off(camera);
    scanner_pwm_stop(pwm_id);
}
