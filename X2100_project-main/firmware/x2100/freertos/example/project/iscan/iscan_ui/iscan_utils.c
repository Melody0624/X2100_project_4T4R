
#include <stdio.h>
#include <os.h>
#include <dfs_posix.h>
#include <stdlib.h>
#include <string.h>

#include "iscan_utils.h"
#include "iscan_ui.h"

#include <stdio.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <math.h>
#include <sys/stat.h>

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>




#define FILE_LIST_PATH_LEN      128

typedef struct {
    char **items;
    int count;
    int cap;
} iscan_name_list_t;

static char *iscan_strdup(const char *src)
{
    size_t len;
    char *dst;

    if (!src)
        return NULL;

    len = strlen(src) + 1;
    dst = malloc(len);
    if (!dst)
        return NULL;

    memcpy(dst, src, len);
    return dst;
}

static int iscan_name_list_push(iscan_name_list_t *list, const char *name)
{
    char **items;
    char *dup;
    int cap;

    if (!list || !name)
        return -1;

    if (list->count >= list->cap) {
        cap = list->cap ? list->cap * 2 : 16;
        items = realloc(list->items, sizeof(char *) * cap);
        if (!items)
            return -1;
        list->items = items;
        list->cap = cap;
    }

    dup = iscan_strdup(name);
    if (!dup)
        return -1;

    list->items[list->count++] = dup;
    return 0;
}

static void iscan_name_list_free(iscan_name_list_t *list)
{
    int i;

    if (!list)
        return;

    for (i = 0; i < list->count; i++)
        free(list->items[i]);

    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static int iscan_name_cmp(const void *a, const void *b)
{
    const char *sa = *(const char * const *)a;
    const char *sb = *(const char * const *)b;

    return strcmp(sa, sb);
}


static int ilv_check_file_extension(const char *file_name, char *extension[], int check_num)
{
    char *dot = strrchr(file_name, '.');
    int i;
    for (i = 0; i < check_num; i++) {
        if (dot && (strcasecmp(dot, extension[i]) == 0)) {
            return 1;
        }
    }

    return 0;
}

static void search_dir_file(lv_ll_t *head, const char *dir, char *ext[], int ext_num, int recursion)
{
    DIR *dp = opendir(dir);
    if (!dp) {
        fprintf(stderr, "open dir %s err\n", dir);
        return;
    }

    iscan_name_list_t dirs = {0};
    iscan_name_list_t files = {0};

    struct dirent *dirp;
    while((dirp = readdir(dp)) != NULL) {
        if (recursion && dirp->d_type == DT_DIR &&
            strcmp(dirp->d_name, ".") != 0 &&
            strcmp(dirp->d_name, "..") != 0) {
            iscan_name_list_push(&dirs, dirp->d_name);
            continue;
        }

        if (!ilv_check_file_extension(dirp->d_name, ext, ext_num))
            continue;
        iscan_name_list_push(&files, dirp->d_name);
    }

    closedir(dp);

    if (dirs.count > 1)
        qsort(dirs.items, dirs.count, sizeof(char *), iscan_name_cmp);
    if (files.count > 1)
        qsort(files.items, files.count, sizeof(char *), iscan_name_cmp);

    if (recursion) {
        int i;
        for (i = 0; i < dirs.count; i++) {
            char new_path[512];
            snprintf(new_path, sizeof(new_path), "%s/%s", dir, dirs.items[i]);
            search_dir_file(head, new_path, ext, ext_num, recursion);
        }
    }

    if (files.count) {
        int i;
        for (i = 0; i < files.count; i++) {
            unsigned int len = strlen(dir) + strlen(files.items[i]) + 2;
            if (len > FILE_LIST_PATH_LEN - 1)
                continue;

            char *new = _lv_ll_ins_tail(head);
            memset(new, 0, head->n_size);
            snprintf(new, len, "%s/%s", dir, files.items[i]);
        }
    }

    iscan_name_list_free(&dirs);
    iscan_name_list_free(&files);

    return;
}

int ilv_file_list_init(lv_ll_t *head, const char *dir_path, char *extension[], int check_num, int user_size)
{
    _lv_ll_init(head, FILE_LIST_PATH_LEN + user_size);

    search_dir_file(head, dir_path, extension, check_num, 0);

    if (_lv_ll_is_empty(head)) {
        return -1;
    }

    return 0;
}

int ilv_file_list_init_recursion(lv_ll_t *head, const char *dir_path, char *extension[], int check_num, int user_size)
{
    _lv_ll_init(head, FILE_LIST_PATH_LEN + user_size + 10);

    search_dir_file(head, dir_path, extension, check_num, 1);

    if (_lv_ll_is_empty(head)) {
        return -1;
    }

    return 0;
}

void *ilv_file_list_get_user_data(lv_ll_t *head, void *node)
{
    if (!head || !node)
        return NULL;

    if (head->n_size <= FILE_LIST_PATH_LEN)
        return NULL;

    return node + FILE_LIST_PATH_LEN;
}


// 获取合适的缩放比例索引
static int get_round_up_index(float scale_coef, const float* scale_array, int array_size)
{
    // 将 scale_coef 限制在 scale_dn 和 scale_up 的范围内
    if (scale_coef < scale_array[0]) {
        scale_coef = scale_array[0];
        return 1;
    }

    if (scale_coef > scale_array[array_size - 1]) {
        scale_coef = scale_array[array_size - 1];
        return array_size;
    }

    int index;
    for (index = 0; index < array_size; index++) {
        if (scale_coef <= scale_array[index]) {
            break;
        }
    }
    return index + 1;
}

/* 缩小/放大到比屏幕较长边大一档, 返回符合的dct_size */
static uint32_t get_dct_size(uint32_t img_w, uint32_t img_h, int rect_w, int rect_h)
{
    uint32_t dct_size;
    uint32_t scale_w, scale_h;

    float scale_dn[] = {0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0};
    float scale_up[] = {1.125, 1.25, 1.375, 1.5, 1.625, 1.75, 1.875, 2.0};

    // uint32_t dis_max_edge = lv_disp_get_hor_res(NULL) > lv_disp_get_ver_res(NULL) ? lv_disp_get_hor_res(NULL) : lv_disp_get_ver_res(NULL);
    uint32_t dis_max_edge = rect_w > rect_h ? rect_w : rect_h;
    uint32_t img_max_edge = img_w > img_h ? img_w : img_h;
    float scale_coef = (float)dis_max_edge / img_max_edge;

    // 计算缩小或放大的比例
    if (scale_coef <= 1)
        dct_size = get_round_up_index(scale_coef, scale_dn, ARRAY_SIZE(scale_dn));
    else
        dct_size = get_round_up_index(scale_coef, scale_up, ARRAY_SIZE(scale_up));

    //若缩放到比屏幕大一档后仍然有宽/高超过MAX_W_LENGTH/MAX_H_LENGTH,需要继续再往下缩小直到满足

    scale_w = (int)ceil((double)img_w * dct_size / 8);
    scale_h = (int)ceil((double)img_h * dct_size / 8);
    while (scale_w > rect_w || scale_h > rect_h) {
        if (dct_size <= 1)
            break;

        if (dct_size > 1) {
            dct_size--;
            scale_w = (int)ceil((double)img_w * dct_size / 8);
            scale_h = (int)ceil((double)img_h * dct_size / 8);
        }
    }

    return dct_size < 1 ? 1 : dct_size;
}


static int pdf_to_jpeg(unsigned char *pdf_data, int pdf_size,
                void **out_jpeg_data, int *out_jpeg_size,
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

    if (!pdf_data || !out_jpeg_data || !out_jpeg_size)
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
                    if (out_width)
                        *out_width = image->w;

                    if (out_height)
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

    if (ref)
        pdf_drop_obj(ctx, ref);
    if (image)
        fz_drop_image(ctx, image);
    if (stream)
        fz_drop_stream(ctx, stream);
    if (doc)
        pdf_drop_document(ctx, doc);

    fz_drop_context(ctx);

    return ret;
}


int ilv_jpeg_decode(char *file, lv_img_dsc_t *img_data, int rect_w, int rect_h)
{
    int ret;
    int is_pdf = 0;
    int fd = open(file, O_CREAT | O_RDWR, 0);
    if (fd < 0)
        return -1;

    struct stat st;
    if (stat(file, &st) != 0) {
        return -1;
    }

    char *dot = strrchr(file, '.');
    if (!file)
        return -1;


    if ((strcasecmp(dot, ".pdf") == 0))
        is_pdf = 1;

    int file_size = st.st_size;

    void *file_data = malloc(file_size);
    assert(file_data);

    ret = read(fd, file_data, file_size);
    close(fd);

    if (ret != file_size) {
        printf("fread err ret = %d\n", ret);
        ret = -1;
        goto free_file_data;
    }

    void *jpeg_buffer = file_data;
    int jpeg_size = file_size;

    if (is_pdf) {
        ret = pdf_to_jpeg(file_data, file_size, &jpeg_buffer, &jpeg_size, NULL, NULL);
        if (ret < 0)
            goto free_file_data;
    }


    struct jpeg_decompress_struct cinfo = {0};

    jpeg_create_decompress(&cinfo);

    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);

    jpeg_mem_src(&cinfo, jpeg_buffer, jpeg_size);

    ret = jpeg_read_header(&cinfo, TRUE);
    if (ret != JPEG_HEADER_OK) {
        printf("read header err\n");
        ret = -1;
        goto destroy_jpeg;
    }

    int scale_w = cinfo.image_width;
    int scale_h = cinfo.image_height;

    bool is_scale = cinfo.image_width != rect_w || cinfo.image_height != rect_h;

    /* 设定参数 */
    if (!cinfo.progressive_mode && is_scale) {
        cinfo.scale_num = get_dct_size(cinfo.image_width, cinfo.image_height, rect_w, rect_h);
        cinfo.scale_denom = 8;

        scale_w = (int)ceil((double)cinfo.image_width * cinfo.scale_num / 8);
        scale_h = (int)ceil((double) cinfo.image_height * cinfo.scale_num / 8);
    }

    cinfo.dct_method = JDCT_IFAST;
    cinfo.do_block_smoothing = FALSE;
    cinfo.out_color_space = JCS_EXT_BGRX;
    cinfo.do_fancy_upsampling = FALSE;

    ret = jpeg_start_decompress(&cinfo);
    if (!ret) {
        printf("start decompress err\n");
        ret = -1;
        goto destroy_jpeg;
    }


    // 解码结果需要缩放后的一整帧大小
    size_t output_buffer_size = cinfo.output_width * cinfo.output_components * cinfo.output_height;
    void *output_buffer = malloc(output_buffer_size);
    assert(output_buffer);

    uint8_t * cur_pos = output_buffer;
    size_t stride = cinfo.output_width * cinfo.output_components;

    /* 逐行解码缩放，逐行拷进output_buffer */
    while(cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, &cur_pos, 1);
        cur_pos += stride;
    }

    /* 解码结束，释放资源 */
    jpeg_finish_decompress(&cinfo);

    img_data->data = output_buffer;
    img_data->data_size = output_buffer_size;
    img_data->header.w = scale_w;
    img_data->header.h = scale_h;
    img_data->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    img_data->header.always_zero = 0;

    ret = 0;

destroy_jpeg:
    jpeg_destroy_decompress(&cinfo);

    if (is_pdf)
        free(jpeg_buffer);

free_file_data:
    free(file_data);

    return ret;
}



lv_style_t *ilv_load_font(const char *path, int height)
{
#if LVGL_VERSION_MAJOR >= 9
    lv_font_t *font = lv_tiny_ttf_create_file(path, height);
#else
    static lv_ft_info_t info;
    lv_ft_info_t info2 = {
        .name = path,
        .weight = height,
        .style = FT_FONT_STYLE_NORMAL,
        .mem = NULL,
    };
    info = info2;
    if(!path || !lv_ft_font_init(&info)) {
        fprintf(stderr, "failed to create font: %s\n", path);
        return NULL;
    }
    lv_font_t *font = info.font;
#endif
    lv_style_t *style = malloc(sizeof(*style));
    lv_style_init(style);

    lv_style_set_text_font(style, font);
    // lv_style_set_text_align(style, LV_TEXT_ALIGN_CENTER);

    return style;
}

void ilv_del_font(lv_style_t *style)
{
    lv_style_value_t value;
    lv_style_get_prop(style, LV_STYLE_TEXT_FONT, &value);
    if (value.ptr)
#if LVGL_VERSION_MAJOR >= 9
        lv_tiny_ttf_destroy((void *)value.ptr);
#else
        lv_ft_font_destroy((void *)value.ptr);
#endif
    lv_style_reset(style);
    free(style);
}


static fz_image *create_jpeg_image_from_data(fz_context *ctx,
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

int ilv_jpeg_to_pdf(void *jpeg_data, int jpeg_size, int width, int height, const char *pdf_path)
{
    fz_context *ctx = NULL;
    pdf_document *doc = NULL;
    fz_image *img = NULL;
    fz_device *dev = NULL;
    pdf_obj *page_obj = NULL;
    int ret = -1;
    int n = 0;

    if (!jpeg_data || !pdf_path || jpeg_size == 0)
        return ret;

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
