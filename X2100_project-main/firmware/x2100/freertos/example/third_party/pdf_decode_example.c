#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <driver/fb.h>
#include <driver/backlight.h>
#include <jpegd_decoder.h>

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#define INPUT_FILE_PATH "/mmcblk0p0/input.pdf"

int load_file_from_sd_dec(const char *filepath, void **out_data, size_t *out_size)
{
    int fd = -1;
    off_t file_size;
    void *buf = NULL;
    int ret = -1;

    fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        printf("failed to open %s\n", filepath);
        return ret;
    }

    file_size = lseek(fd, 0, SEEK_END);
    if (file_size <= 0 || lseek(fd, 0, SEEK_SET) < 0) {
        printf("failed to seek %s\n", filepath);
        goto cleanup;
    }

    buf = malloc(file_size);
    if (!buf) {
        printf("failed to allocate %ld bytes\n", (long)file_size);
        goto cleanup;
    }

    if (read(fd, buf, file_size) != file_size) {
        printf("failed to read %s\n", filepath);
        goto cleanup;
    }

    *out_data = buf;
    *out_size = (size_t)file_size;
    ret = 0;
    buf = NULL;

cleanup:
    if (buf)
        free(buf);
    if (fd >= 0)
        close(fd);

    return ret;
}

int pdf_to_jpeg(unsigned char *pdf_data, size_t pdf_size,
                unsigned char **out_jpeg_data, size_t *out_jpeg_size,
                int *out_width, int *out_height)
{
    fz_context *ctx = NULL;
    pdf_document *doc = NULL;
    fz_stream *stream = NULL;
    fz_image *image = NULL;
    pdf_obj *ref = NULL;
    unsigned char *src_data;
    size_t src_len;
    int found = 0; // 标记是否找到
    int ret = -1;

    if (!pdf_data || !out_jpeg_data || !out_jpeg_size || !out_width || !out_height)
        return ret;

    *out_jpeg_data = NULL;
    *out_jpeg_size = 0;

    /* 创建上下文 */
    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (!ctx) {
        printf("cannot create context\n");
        return ret;
    }

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);

        /* 从内存流打开文档 */
        stream = fz_open_memory(ctx, pdf_data, pdf_size);
        doc = pdf_open_document_with_stream(ctx, stream);

        int count = pdf_count_objects(ctx, doc);

        /* 遍历所有对象查找 Image */
        for (int i = 1; i < count && !found; i++)
        {
            ref = pdf_new_indirect(ctx, doc, i, 0);

            /* 快速检查 Subtype */
            pdf_obj *subtype = pdf_dict_get(ctx, ref, PDF_NAME(Subtype));
            if (!pdf_name_eq(ctx, subtype, PDF_NAME(Image)))
            {
                pdf_drop_obj(ctx, ref);
                ref = NULL;
                continue;
            }

            /* 加载 image */
            image = pdf_load_image(ctx, doc, ref);

            /* 取 compressed buffer */
            fz_compressed_buffer *cbuf = fz_compressed_image_buffer(ctx, image);
            if (cbuf && cbuf->params.type == FZ_IMAGE_JPEG)
            {
                src_len = fz_buffer_storage(ctx, cbuf->buffer, &src_data);
                if (src_len > 0 && src_data) {
                    *out_width = image->w;
                    *out_height = image->h;
                    found = 1;

                    /* 复制数据 */
                    *out_jpeg_data = malloc(src_len);
                    if(*out_jpeg_data) {
                        memcpy(*out_jpeg_data, src_data, src_len);
                        *out_jpeg_size = src_len;
                        ret = 0;
                    } else {
                        fz_throw(ctx, FZ_ERROR_GENERIC, "malloc failed");
                    }
                }
            }
            fz_drop_image(ctx, image);
            image = NULL;
            pdf_drop_obj(ctx, ref);
            ref = NULL;
        }

        if (!found)
            fz_throw(ctx, FZ_ERROR_GENERIC, "no jpeg image found");
    }
    fz_catch(ctx)
    {
        printf("pdf decode error: %s\n", fz_caught_message(ctx));
        if (out_jpeg_data && *out_jpeg_data) {
            free(*out_jpeg_data);
            *out_jpeg_data = NULL;
        }
        ret = -1;
    }

    if (ref) pdf_drop_obj(ctx, ref);
    if (image) fz_drop_image(ctx, image);
    if (stream) fz_drop_stream(ctx, stream);
    if (doc) pdf_drop_document(ctx, doc);
    fz_drop_context(ctx);

    return ret;
}

void jpegd_decode_display_to_fb(unsigned char *data, size_t size, int width, int height)
{
    struct fb_info fb_info;
    struct fb_handle *fb;
    struct backlight *fb_backlight;
    struct jpegd_decoder *decoder;
    struct jpegd_decoder_output out;
    int ret;

    if (!data || size == 0)
        return;

    /* 打开背光 */
    fb_backlight = backlight_open("backlight_gpio0");
    if (!fb_backlight) {
        printf("unable to open backlight!\n");
        return;
    }

    /* 初始化fb */
    fb = fb_open("fb0");
    if (!fb) {
        printf("unable to open fb0!\n");
        return;
    }
    fb_get_info(fb, &fb_info);
    fb_enable(fb);

    struct jpegd_decoder_param param = {
        .width = width,
        .height = height,
        .out_fmt = JPEGD_PIX_FMT_BGRA_8888,
        .crop = 0,
    };

    decoder = jpegd_decoder_init(&param);
    if (!decoder) {
        printf("jpeg decoder init failed\n");
        while(!fb_is_enable(fb));
        fb_disable(fb);
    }

    out.data = fb_info.fb_mem;
    out.data_size = fb_info.bytes_per_frame;

    ret = jpegd_decoder_decode(decoder, (void *)data, size, &out);
    if (ret < 0) {
        printf("jpeg decode failed\n");
        goto cleanup_decoder;
    }

    fb_pan_display(fb, 0);
    backlight_set_brightness(fb_backlight, backlight_get_maxbrightness(fb_backlight));

cleanup_decoder:
    jpegd_decoder_deinit(decoder);
}

/*
    1.读取 pdf 文件到内存
    2.遍历对象找到第一张 jpeg 图片,提取 jpeg 压缩流
    3.调用 jpeg 硬件解码器将 jpeg 解码为 BGRA8888 格式,写入fb显示
*/
void pdf_decode_test(void)
{
    void *pdf_data = NULL;
    size_t pdf_size = 0;
    unsigned char *jpeg_data = NULL;
    size_t jpeg_size = 0;
    int width;
    int height;

    /* 读取 pdf */
    if (load_file_from_sd_dec(INPUT_FILE_PATH, &pdf_data, &pdf_size) < 0) {
        printf("failed to load pdf\n");
        return;
    }

    /* 提取 jpeg */
    if (pdf_to_jpeg(pdf_data, pdf_size, &jpeg_data, &jpeg_size, &width, &height) < 0) {
        printf("failed to extract jpeg from pdf\n");
        goto cleanup_pdf;
    }

    /* jpeg解码显示 */
    jpegd_decode_display_to_fb(jpeg_data, jpeg_size, width, height);

    free(jpeg_data);

cleanup_pdf:
    free(pdf_data);
}
