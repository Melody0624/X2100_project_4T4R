#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <jpege_encoder.h>

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#define WIDTH 720
#define HEIGHT 1280

#define INPUT_NV12_PATH "/mmcblk0p0/720x1280_nv12.yuv"
#define OUTPUT_PDF_PATH "output.pdf"

int load_file_from_sd_enc(const char *filepath, void **out_data, size_t *out_size)
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

/**
 * 从 jpeg 数据头解析基本信息
 * 返回: 0 成功, -1 失败
 *
 * jpeg 格式: SOI (FFD8) + [段]*
 * SOF0 段: FF C0 length(2) precision(1) height(2) width(2) components(1) [...]
 */
int parse_jpeg_header(const uint8_t *data, size_t size,
                      int *width, int *height, int *n)
{
    size_t i = 0;

    /* 最小 jpeg 头: SOI + SOF0 段 */
    if (size < 11)
        return -1;

    /* 检查 SOI 标记 FFD8 */
    if (data[0] != 0xFF || data[1] != 0xD8)
        return -1;

    i = 2;
    while (i < size - 9) {
        /* 跳过填充字节 FF */
        while (i < size && data[i] == 0xFF)
            i++;

        if (i >= size)
            break;

        uint8_t marker = data[i];
        i++;

        /* SOF0 (基线), SOF1 (扩展顺序), SOF2 (渐进) */
        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            /* length (2 bytes, big-endian) - 跳过 */
            /* precision (1 byte) - 通常是 8 */

            *height = (data[i + 3] << 8) | data[i + 4];
            *width  = (data[i + 5] << 8) | data[i + 6];
            int components = data[i + 7];  /* 1=灰度, 3=YCbCr/RGB, 4=CMYK */

            if (components == 1)
                *n = 1;  /* Grayscale */
            else if (components == 3)
                *n = 3;  /* YCbCr 或 RGB */
            else if (components == 4)
                *n = 4;  /* CMYK */
            else
                return -1;

            return 0;
        }
        /* 其他段需要跳过 */
        else if (marker != 0x00 && marker != 0x01 &&
                 !(marker >= 0xD0 && marker <= 0xD9)) {
            if (i + 1 >= size)
                break;
            uint16_t seg_len = (data[i] << 8) | data[i + 1];
            i += seg_len;
        }
    }

    return -1;
}

/**
 * 从原始 jpeg 数据创建压缩图像
 */
fz_image *create_jpeg_image_from_data(fz_context *ctx,
    const unsigned char *jpeg_data, size_t jpeg_size,
    int width, int height, int n)
{
    fz_compressed_buffer *cbuffer = NULL;
    fz_buffer *buffer = NULL;
    fz_image *image = NULL;

    fz_var(buffer);
    fz_var(cbuffer);

    fz_try(ctx)
    {
        /* 创建缓冲区并复制 jpeg 数据 */
        buffer = fz_new_buffer_from_copied_data(ctx, jpeg_data, jpeg_size);

        /* 分配压缩缓冲区结构 */
        cbuffer = fz_malloc_struct(ctx, fz_compressed_buffer);
        cbuffer->buffer = buffer;
        buffer = NULL;
        cbuffer->params.type = FZ_IMAGE_JPEG;
        cbuffer->params.u.jpeg.color_transform = -1;
        cbuffer->params.u.jpeg.invert_cmyk = 0;

        /* 根据通道数确定颜色空间 */
        fz_colorspace *cs = NULL;
        if (n == 1)
            cs = fz_device_gray(ctx);
        else if (n == 3)
            cs = fz_device_rgb(ctx);
        else if (n == 4)
            cs = fz_device_cmyk(ctx);
        else
            fz_throw(ctx, FZ_ERROR_GENERIC, "unsupported number of components: %d", n);

        /* 创建图像 */
        image = fz_new_image_from_compressed_buffer(ctx,
            width, height,       /* 尺寸 */
            8,                   /* 每分量位数 */
            cs,                  /* 颜色空间 */
            96, 96,              /* X/Y 分辨率 */
            0,                   /* 插值 */
            0,                   /* 图像掩码 */
            NULL,                /* 解码数组 */
            NULL,                /* 颜色键 */
            cbuffer,             /* 压缩缓冲区 */
            NULL);               /* 掩码图像 */

        cbuffer = NULL;  /* image 接管了 cbuffer */
    }
    fz_catch(ctx)
    {
        fz_drop_buffer(ctx, buffer);
        if (cbuffer) {
            fz_drop_buffer(ctx, cbuffer->buffer);
            fz_free(ctx, cbuffer);
        }
        fz_rethrow(ctx);
    }

    return image;
}

/**
 * 将 jpeg 数据转换为 pdf 文件
 */
int jpeg_to_pdf(void *jpeg_data, size_t jpeg_size, const char *pdf_path)
{
    fz_context *ctx = NULL;
    pdf_document *doc = NULL;
    fz_image *img = NULL;
    fz_device *dev = NULL;
    pdf_obj *page_obj = NULL;
    int ret = -1;
    int width = 0, height = 0, n = 0;

    if (!jpeg_data || !pdf_path || jpeg_size == 0)
        return ret;

    /* 解析 jpeg 头获取参数 */
    if (parse_jpeg_header(jpeg_data, jpeg_size, &width, &height, &n) < 0) {
        printf("failed to parse jpeg header\n");
        return ret;
    }

    /* 创建上下文 */
    ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (!ctx) {
        printf("cannot create context\n");
        return ret;
    }

    fz_try(ctx)
    {
        fz_register_document_handlers(ctx);

        /* 创建空 pdf 文档 */
        doc = pdf_create_document(ctx);

        /* 直接嵌入 jpeg 数据 */
        img = create_jpeg_image_from_data(ctx, jpeg_data, jpeg_size, width, height, n);
        if (!img)
            fz_throw(ctx, FZ_ERROR_GENERIC, "cannot create image from jpeg data");

        /* 创建页面 */
        fz_rect mediabox = fz_make_rect(0, 0, (float)width, (float)height);
        pdf_obj *resources = NULL;
        fz_buffer *contents = NULL;

        /* 创建写入设备 */
        dev = pdf_page_write(ctx, doc, mediabox, &resources, &contents);

        /* 图片填满页面 */
        fz_matrix matrix = fz_scale((float)width, (float)height);

        /* 绘制图片 */
        fz_fill_image(ctx, dev, img, matrix, 1.0f, fz_default_color_params);

        /* 关闭设备 */
        fz_close_device(ctx, dev);
        dev = NULL;

        /* 添加页面 */
        page_obj = pdf_add_page(ctx, doc, mediabox, 0, resources, contents);
        pdf_insert_page(ctx, doc, -1, page_obj);

        /* 保存 pdf */
        pdf_save_document(ctx, doc, pdf_path, NULL);

        ret = 0;
    }
    fz_catch(ctx)
    {
        printf("pdf encode error: %s\n", fz_caught_message(ctx));
        if (dev)
            fz_close_device(ctx, dev);
    }

    /* 清理资源 */
    if (dev) fz_drop_device(ctx, dev);
    if (img) fz_drop_image(ctx, img);
    if (page_obj) pdf_drop_obj(ctx, page_obj);
    if (doc) pdf_drop_document(ctx, doc);
    if (ctx) fz_drop_context(ctx);

    return ret;
}

int jpeg_encode(void *nv12_data, size_t nv12_size, struct jpege_encoder **encoder, struct jpege_encoder_output **en_out)
{
    int ret = -1;
    struct jpege_encoder_param en_param = {
        .width = WIDTH,
        .height = HEIGHT,
        .in_fmt = JPEGE_PIX_FMT_NV12,
        .quality = 90,
    };

    if (!nv12_data || !encoder || !en_out)
        return ret;

    *encoder = jpege_encoder_init(&en_param);
    if (!(*encoder)) {
        printf("jpeg encoder init failed\n");
        return ret;
    }

    *en_out = jpege_encoder_alloc_output_buf(*encoder);
    if (!(*en_out)) {
        printf("jpeg encoder alloc buf failed\n");
        goto err_deinit;
    }

    ret = jpege_encoder_encode(*encoder, nv12_data, nv12_size, *en_out);
    if (ret < 0) {
        printf("jpeg encode failed\n");
        goto err_free_buf;
    }

    return 0;

err_free_buf:
    if (*en_out) {
        jpege_encoder_free_output_buf(*en_out);
        *en_out = NULL;
    }
err_deinit:
    if (*encoder) {
        jpege_encoder_deinit(*encoder);
        *encoder = NULL;
    }
    return ret;
}

/*
    1.读取 nv12 文件
    2.编码为 jpeg
    3.使用 mupdf 创建文档并将 jpeg 嵌入页面,保存为 pdf 文件
*/
void pdf_encode_test(void)
{
    void *nv12_data = NULL;
    size_t nv12_size = 0;
    struct jpege_encoder *encoder = NULL;
    struct jpege_encoder_output *en_out = NULL;

    /* 读取 nv12 文件 */
    if (load_file_from_sd_enc(INPUT_NV12_PATH, &nv12_data, &nv12_size) < 0) {
        printf("failed to load nv12 file\n");
        return;
    }

    /* 编码为 jpeg */
    if (jpeg_encode(nv12_data, nv12_size, &encoder, &en_out) < 0) {
        printf("failed to encode jpeg\n");
        goto cleanup;
    }

    /* 生成 pdf */
    if (jpeg_to_pdf(en_out->data, en_out->data_size, OUTPUT_PDF_PATH) < 0)
        printf("failed to create pdf\n");

cleanup:
    if (en_out) jpege_encoder_free_output_buf(en_out);
    if (encoder) jpege_encoder_deinit(encoder);
        free(nv12_data);
}
