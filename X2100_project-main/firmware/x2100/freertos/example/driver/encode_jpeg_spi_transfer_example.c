#include <stdio.h>
#include <common.h>
#include <driver/fb.h>
#include <driver/spi.h>
#include <driver/gpio.h>
#include <driver/cache.h>
#include <driver/rotator.h>
#include <driver/backlight.h>
#include <driver/camera_isp.h>
#include <helix/helix_jpeg_encoder.h>
#include <semaphore.h>


#define ISP_WIDTH 1280
#define ISP_HEIGHT 720


static sem_t transfer_start;
static sem_t transfer_done;

static struct spi_config_data spi_config = {
    .id = 0,
    .cs_pin = GPIO_PB(28),
    .clk_rate = 50 * 1000 * 1000,
    .cs_valid_level = Spi_valid_high,
    .tx_endian = Spi_endian_msb_first,
    .rx_endian = Spi_endian_msb_first,
    .bits_per_word = 8,
    .spi_pha = 0,
    .spi_pol = 0,
    .loop_mode = 0,
};

static struct spi_message msg[] = {
    [0] = {
        .cs_change = 1,
        .use_dma = 3,  /*50M 速率 dma unit 必须32字节以上， 否则会recive overrun*/
    },
};

static struct rotator_config_data rotator_config = {
    .frame_height = ISP_HEIGHT,
    .frame_width = ISP_WIDTH,
    .src_stride = ISP_WIDTH,
    .dst_stride = ISP_HEIGHT,
    .src_fmt = ROTATOR_NV12,
    .dst_fmt = ROTATOR_NV12,
    .horizontal_mirror = ROTATOR_NO_MIRROR,
    .vertical_mirror = ROTATOR_NO_MIRROR,
    .rotate_angle = ROTATOR_ANGLE_90,
};

/* 使用最小分辨率8*8 */
static struct frame_image_format isp_fmt = {
    .width = ISP_WIDTH,
    .height = ISP_HEIGHT,
    .scaler.enable = 1,
    .scaler.width = ISP_WIDTH,
    .scaler.height = ISP_HEIGHT,
    .pixel_format       = CAMERA_PIX_FMT_NV12,
    .frame_nums         = 2,
};

/*分辨率为旋转90度后的*/
static struct helix_jpeg_encoder_param jpeg_param = {
    .width = ISP_HEIGHT,
    .height = ISP_WIDTH,
    .compress_quality = 80,
};


void spi_transfer_mjpeg(void *data)
{
    struct spi_device *spi = spi_register(&spi_config);
    assert(spi);

    uint64_t start_time = 0;
    int fps = 0;
    int is_first = 1;

    while(1) {
        sem_wait(&transfer_start);

        spi_transfer(spi, msg, 1);

        if (is_first) {
            start_time = systick_get_time_ms();
            is_first = 0;
        }

        fps++;
        if (systick_get_time_ms() - start_time >= 1000) {
            printf("spi transfser fps = %d\n", fps);
            start_time = systick_get_time_ms();
            fps = 0;
        }

        sem_post(&transfer_done);
    }
}

void encode_jpeg_spi_transfer_test(void)
{
    int ret;
    int size;
    struct fb_handle *fb;
    camera_hd_t *camera_handle;
    struct camera_info *info;

    char fmt_a, fmt_b, fmt_c, fmt_d;

    void *rotator_vaddr = NULL;

    struct helix_jpeg_encoder *encoder;

    camera_handle = isp_detect(0, 0);
    assert(camera_handle);

    ret = isp_set_format(camera_handle, &isp_fmt);
    assert(!ret);

    ret = isp_request_buffer(camera_handle, &isp_fmt);
    assert(!ret);

    info = isp_get_info(camera_handle);
    assert(info);

    fmt_a = (char)(info->data_fmt >> 0);
    fmt_b = (char)(info->data_fmt >> 8);
    fmt_c = (char)(info->data_fmt >> 16);
    fmt_d = (char)(info->data_fmt >> 24);
    printf("channel         = %s\n", (char *)camera_handle->ptr);
    printf("sensor_name     = %s\n", info->name);
    printf("width           = %d\n", info->width);
    printf("height          = %d\n", info->height);
    printf("fps             = %d\n", info->fps);
    printf("data_fmt        = %c%c%c%c\n", fmt_a, fmt_b, fmt_c, fmt_d);
    printf("line_length     = %d\n", info->line_length);
    printf("frame_size      = %d\n", info->frame_size);
    printf("frame_align_size= %d\n", info->frame_align_size);

    ret = isp_power_on(camera_handle);
    assert(!ret);

    ret = isp_stream_on(camera_handle);
    assert(!ret);

    fb = fb_open("fb0");
    assert(fb);

    fb_enable(fb);


    /* 申请旋转使用的buf */
    rotator_vaddr = cache_align_malloc(info->frame_size);
    assert(rotator_vaddr);


    int out_size = info->width * info->height;
    void *out_buf = cache_align_malloc(out_size);
    assert(out_buf);

    encoder = helix_jpeg_encoder_init(&jpeg_param);
    assert(encoder);

    sem_init(&transfer_done, 0, 1);
    sem_init(&transfer_start, 0, 0);


    struct backlight *m_backlight = backlight_open("backlight_gpio0");
    if (m_backlight) {
        int level = backlight_get_maxbrightness(m_backlight);
        printf("backlight level: %d\n", level);
        backlight_set_brightness(m_backlight, level);
    }

    thread_create("spi_transfef", 2048, spi_transfer_mjpeg, NULL);

    while (1) {
        void *mem = isp_wait_frame(camera_handle);
        if (mem) {
            rotator_config.src_buf = mem;
            rotator_config.dst_buf = rotator_vaddr;
            rotator_conversion(&rotator_config);
            isp_put_frame(camera_handle, mem);
            void *y_mem = rotator_vaddr;
            void *uv_mem = rotator_vaddr + info->width * info->height;

            struct lcdc_layer layer_cfg = {
                .fb_fmt = fb_fmt_NV12,
                .xres = info->height,
                .yres = info->width,
                .xpos = 0,
                .ypos = 0,

                .layer_order = lcdc_layer_bottom,
                .layer_enable = 1,

                .y = {
                    .mem = y_mem,
                    .stride = info->height,
                },

                .uv = {
                    .mem = uv_mem,
                    .stride = info->height,
                },

                .alpha = {
                    .enable = 0,
                    .value = 0xff,
                },
            };

            if (!fb_set_config(fb, &layer_cfg)) {
                fb_enable_config(fb);
                fb_pan_display(fb, 0);
            }

            sem_wait(&transfer_done);

            memset(out_buf, 0, out_size);

            size = helix_jpeg_encoder_encode(encoder, rotator_vaddr, out_buf, out_size);
            if (size <= 0) {
                 printf("jpeg encoding failed %d\n", size);
                 continue;
            }

            /*这里rx_buf = tx_buf 可以省一帧buffer的数据。
              因为spi 控制器是由tx 来触发rx的。并且tx和rx有fifo，
              当发送时开始工作了，意味已经写了一定的数据到fifo了。
              接收时，会等待fifo到一定数据，才搬到dma。
              所以接收的数据永远不会覆盖要发送的数据
            */
            msg[0].tlen = ALIGN(size, 32);
            msg[0].tx_buf = out_buf;

            msg[0].rlen = ALIGN(size, 32);
            msg[0].rx_buf = out_buf;

            sem_post(&transfer_start);
        }
    }

}