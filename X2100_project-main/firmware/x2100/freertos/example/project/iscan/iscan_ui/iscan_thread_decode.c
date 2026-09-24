#include "iscan_thread_decode.h"
#include "iscan_utils.h"
#include <os.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct iscan_thread_decode_handle {
    int rect_w;
    int rect_h;
    lv_ll_t *list;
    char *dir_path;
    int cache_total;
    void *center;
    void **window;
    void *decoding;
    thread_ptr_t thread;
    struct mutex lock;
    semaphore_t notify;
    semaphore_t done;
    int pause;
    int stop;
} iscan_dec_t;

static char *iscan_ext_name[] = {".jpg", ".jpeg", ".pdf"};

static int iscan_decode_is_valid(const char *path)
{
    const char *dot = NULL;

    if (!path)
        return 0;

    dot = strrchr(path, '.');
    if (!dot)
        return 0;

    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0)
        return 1;

    if ((strcasecmp(dot, ".pdf") == 0))
        return 1;


    return 0;
}

static void iscan_decode_clear_img_dsc(lv_img_dsc_t *dsc)
{
    if (!dsc)
        return;

    if (dsc->data)
        free((void *)dsc->data);

    memset(dsc, 0, sizeof(*dsc));
}

static int iscan_decode_node_in_window(void *node, void **window, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (window[i] == node)
            return 1;
    }

    return 0;
}

static int iscan_decode_build_window(lv_ll_t *list, void *center, void **window, int total)
{
    int count = 0;
    int half;
    int i;
    void *node;

    if (!list || !center || !window || total <= 0)
        return 0;

    if (total <= 1) {
        window[0] = center;
        return 1;
    }

    half = total / 2;

    window[count++] = center;

    node = center;
    for (i = 0; i < half; i++) {
        node = _lv_ll_get_prev(list, node);
        if (!node)
            node = _lv_ll_get_tail(list);
        if (!node)
            break;
        if (!iscan_decode_node_in_window(node, window, count))
            window[count++] = node;
    }

    node = center;
    for (i = 0; i < half; i++) {
        node = _lv_ll_get_next(list, node);
        if (!node)
            node = _lv_ll_get_head(list);
        if (!node)
            break;
        if (!iscan_decode_node_in_window(node, window, count))
            window[count++] = node;
    }

    return count;
}

static int iscan_decode_effective_total(iscan_dec_t *dec, int len)
{
    int total;

    if (!dec || len <= 0)
        return 0;

    total = dec->cache_total;
    if (total < 1)
        total = 1;
    if (total > len)
        total = len;
    if ((total % 2) == 0)
        total -= 1;
    if (total < 1)
        total = 1;

    return total;
}

static int iscan_decode_get_index(lv_ll_t *list, void *node)
{
    int index = 1;
    void *cur;

    if (!list || !node)
        return 0;

    cur = _lv_ll_get_head(list);
    while (cur) {
        if (cur == node)
            return index;
        cur = _lv_ll_get_next(list, cur);
        index++;
    }

    return 0;
}

static void iscan_decode_notify(iscan_dec_t *dec)
{
    if (!dec)
        return;

    semaphore_post(&dec->notify);
}

static void iscan_decode_thread(void *data)
{
    iscan_dec_t *dec = (iscan_dec_t *)data;

    while (1) {
        semaphore_wait_timeout(&dec->notify, 200);

        if (dec->stop)
            break;

        while (1) {
            void *target = NULL;
            char *path = NULL;
            lv_img_dsc_t tmp;
            int ret = -1;
            int len;
            int total;
            int count;
            void *node;
            int i;

            mutex_lock(&dec->lock);

            if (!dec->list || _lv_ll_is_empty(dec->list)) {
                mutex_unlock(&dec->lock);
                break;
            }

            if (dec->pause) {
                mutex_unlock(&dec->lock);
                break;
            }

            if (!dec->center)
                dec->center = _lv_ll_get_head(dec->list);

            if (!dec->center) {
                mutex_unlock(&dec->lock);
                break;
            }

            len = _lv_ll_get_len(dec->list);
            total = iscan_decode_effective_total(dec, len);
            count = iscan_decode_build_window(dec->list, dec->center, dec->window, total);
            if (count <= 0) {
                mutex_unlock(&dec->lock);
                break;
            }

            node = _lv_ll_get_head(dec->list);
            while (node) {
                if (!iscan_decode_node_in_window(node, dec->window, count)) {
                    lv_img_dsc_t *dsc = ilv_file_list_get_user_data(dec->list, node);
                    if (dsc && dsc->data)
                        iscan_decode_clear_img_dsc(dsc);
                }
                node = _lv_ll_get_next(dec->list, node);
            }

            for (i = 0; i < count; i++) {
                path = (char *)dec->window[i];
                if (!iscan_decode_is_valid(path))
                    continue;

                lv_img_dsc_t *dsc = ilv_file_list_get_user_data(dec->list, dec->window[i]);
                if (!dsc)
                    continue;

                if (dsc->data)
                    continue;

                target = dec->window[i];
                break;
            }

            if (!target) {
                mutex_unlock(&dec->lock);
                break;
            }

            dec->decoding = target;
            path = (char *)target;
            mutex_unlock(&dec->lock);

            memset(&tmp, 0, sizeof(tmp));
            ret = ilv_jpeg_decode(path, &tmp, dec->rect_w, dec->rect_h);

            mutex_lock(&dec->lock);
            if (dec->decoding == target) {
                lv_img_dsc_t *dsc = ilv_file_list_get_user_data(dec->list, target);
                if (dsc && ret == 0) {
                    if (dsc->data)
                        iscan_decode_clear_img_dsc(dsc);
                    *dsc = tmp;
                    tmp.data = NULL;
                }
                dec->decoding = NULL;
                semaphore_post(&dec->done);
            }
            mutex_unlock(&dec->lock);

            if (tmp.data)
                free((void *)tmp.data);
        }
    }

    thread_delete(NULL);
}

iscan_dec_t *iscan_dec_create(int rect_w, int rect_h, const char *dir_path, int cache_total)
{
    iscan_dec_t *dec;
    size_t path_len;
    int ret;

    if (!dir_path)
        return NULL;

    if (cache_total < 1)
        cache_total = 1;

    dec = calloc(1, sizeof(*dec));
    if (!dec)
        return NULL;

    dec->rect_w = rect_w;
    dec->rect_h = rect_h;
    dec->list = calloc(1, sizeof(*dec->list));
    if (!dec->list) {
        free(dec);
        return NULL;
    }

    path_len = strlen(dir_path) + 1;
    dec->dir_path = malloc(path_len);
    if (!dec->dir_path) {
        free(dec->list);
        free(dec);
        return NULL;
    }
    memcpy(dec->dir_path, dir_path, path_len);

    ret = ilv_file_list_init(dec->list, dir_path, iscan_ext_name, 3, sizeof(lv_img_dsc_t));
    (void)ret;
    dec->cache_total = cache_total;
    dec->window = calloc(cache_total, sizeof(void *));
    if (!dec->window) {
        free(dec);
        return NULL;
    }

    mutex_init(&dec->lock);
    semaphore_init(&dec->notify, 0);
    semaphore_init(&dec->done, 0);

    dec->thread = thread_create("iscan_thread_decode", 8 * 1024, iscan_decode_thread, dec);

    iscan_decode_notify(dec);

    return dec;
}

void iscan_dec_destroy(iscan_dec_t *dec)
{
    if (!dec)
        return;

    dec->stop = 1;
    iscan_decode_notify(dec);
    if (dec->thread)
        thread_join(dec->thread, NULL);

    mutex_lock(&dec->lock);
    if (dec->list) {
        void *node = _lv_ll_get_head(dec->list);
        while (node) {
            lv_img_dsc_t *dsc = ilv_file_list_get_user_data(dec->list, node);
            if (dsc && dsc->data)
                iscan_decode_clear_img_dsc(dsc);
            node = _lv_ll_get_next(dec->list, node);
        }
        _lv_ll_clear(dec->list);
    }
    mutex_unlock(&dec->lock);

    free(dec->dir_path);
    free(dec->list);
    free(dec->window);
    free(dec);
}

int iscan_dec_add(iscan_dec_t *dec, const char *path)
{
    void *node;
    char *dst;
    char *user;
    size_t cap;

    if (!dec || !dec->list || !path)
        return -1;

    mutex_lock(&dec->lock);
    node = _lv_ll_ins_tail(dec->list);
    if (!node) {
        mutex_unlock(&dec->lock);
        return -1;
    }

    dst = (char *)node;
    user = ilv_file_list_get_user_data(dec->list, node);
    cap = user ? (size_t)(user - dst) : dec->list->n_size;
    if (cap > 0) {
        strncpy(dst, path, cap - 1);
        dst[cap - 1] = '\0';
    }

    if (user)
        memset(user, 0, sizeof(lv_img_dsc_t));

    if (!dec->center)
        dec->center = node;

    mutex_unlock(&dec->lock);

    iscan_decode_notify(dec);

    return 0;
}

int iscan_dec_del(iscan_dec_t *dec, void *node)
{
    void *next;
    lv_img_dsc_t *dsc;

    if (!dec || !dec->list || !node)
        return -1;

    while (1) {
        mutex_lock(&dec->lock);
        if (dec->decoding == node) {
            mutex_unlock(&dec->lock);
            semaphore_wait(&dec->done);
            continue;
        }
        break;
    }

    if (dec->center == node) {
        next = _lv_ll_get_next(dec->list, node);
        if (!next)
            next = _lv_ll_get_prev(dec->list, node);
        dec->center = next;
    }

    dsc = ilv_file_list_get_user_data(dec->list, node);
    if (dsc && dsc->data)
        iscan_decode_clear_img_dsc(dsc);

    _lv_ll_remove(dec->list, node);
    lv_mem_free(node);

    mutex_unlock(&dec->lock);

    iscan_decode_notify(dec);

    return 0;
}

void iscan_dec_clear(iscan_dec_t *dec)
{
    void *node;
    void *next;

    if (!dec || !dec->list)
        return;

    while (1) {
        mutex_lock(&dec->lock);
        dec->pause = 1;
        if (dec->decoding) {
            mutex_unlock(&dec->lock);
            semaphore_wait(&dec->done);
            continue;
        }
        break;
    }

    node = _lv_ll_get_head(dec->list);
    while (node) {
        next = _lv_ll_get_next(dec->list, node);
        lv_img_dsc_t *dsc = ilv_file_list_get_user_data(dec->list, node);
        if (dsc && dsc->data)
            iscan_decode_clear_img_dsc(dsc);
        node = next;
    }

    _lv_ll_clear(dec->list);
    dec->center = NULL;
    dec->pause = 0;

    mutex_unlock(&dec->lock);

    iscan_decode_notify(dec);
}

int iscan_dec_set_center(iscan_dec_t *dec, iscan_dec_center_t type)
{
    int len;
    int target;
    int i;
    void *node;

    if (!dec || !dec->list)
        return -1;

    mutex_lock(&dec->lock);
    switch (type) {
    case ISCAN_DEC_HEAD:
        dec->center = _lv_ll_get_head(dec->list);
        break;
    case ISCAN_DEC_TAIL:
        dec->center = _lv_ll_get_tail(dec->list);
        break;
    case ISCAN_DEC_CENTER:
        len = _lv_ll_get_len(dec->list);
        if (len <= 0) {
            dec->center = NULL;
            break;
        }
        target = len / 2 + 1;
        node = _lv_ll_get_head(dec->list);
        for (i = 1; node && i < target; i++)
            node = _lv_ll_get_next(dec->list, node);
        dec->center = node;
        break;
    default:
        mutex_unlock(&dec->lock);
        return -1;
    }
    mutex_unlock(&dec->lock);

    iscan_decode_notify(dec);
    return 0;
}

lv_img_dsc_t *iscan_dec_get_img(iscan_dec_t *dec, void *node)
{
    lv_img_dsc_t *img;

    if (!dec || !node)
        return NULL;

    mutex_lock(&dec->lock);
    img = ilv_file_list_get_user_data(dec->list, node);
    mutex_unlock(&dec->lock);

    return img;
}

void *iscan_dec_get_center(iscan_dec_t *dec, int *index_out)
{
    void *center;
    int index;

    if (!dec)
        return NULL;

    mutex_lock(&dec->lock);
    center = dec->center;
    index = iscan_decode_get_index(dec->list, center);
    mutex_unlock(&dec->lock);

    if (index_out)
        *index_out = index;

    return center;
}

void *iscan_dec_get_next(iscan_dec_t *dec, int *index_out)
{
    void *next;
    int index;

    if (!dec)
        return NULL;

    if (!dec->list)
        return  NULL;

    mutex_lock(&dec->lock);
    if (!dec->center)
        dec->center = _lv_ll_get_head(dec->list);

    if (!dec->center) {
        mutex_unlock(&dec->lock);
        return NULL;
    }

    next = _lv_ll_get_next(dec->list, dec->center);
    if (!next)
        next = _lv_ll_get_head(dec->list);
    dec->center = next;
    index = iscan_decode_get_index(dec->list, dec->center);
    mutex_unlock(&dec->lock);

    iscan_decode_notify(dec);
    if (index_out)
        *index_out = index;
    return next;
}

void *iscan_dec_get_prev(iscan_dec_t *dec, int *index_out)
{
    void *prev;
    int index;

    if (!dec)
        return NULL;

    if (!dec->list)
        return NULL;


    mutex_lock(&dec->lock);
    if (!dec->center)
        dec->center = _lv_ll_get_tail(dec->list);

    if (!dec->center) {
        mutex_unlock(&dec->lock);
        return NULL;
    }


    prev = _lv_ll_get_prev(dec->list, dec->center);
    if (!prev)
        prev = _lv_ll_get_tail(dec->list);
    dec->center = prev;
    index = iscan_decode_get_index(dec->list, dec->center);
    mutex_unlock(&dec->lock);

    iscan_decode_notify(dec);
    if (index_out)
        *index_out = index;
    return prev;
}

int iscan_dec_get_nums(iscan_dec_t *dec)
{
    int len;
    if (!dec || !dec->list)
        return 0;

    mutex_lock(&dec->lock);

    len = _lv_ll_get_len(dec->list);

    mutex_unlock(&dec->lock);

    return len;
}

int iscan_dec_rescan(iscan_dec_t *dec)
{
    void *node;
    void *next;
    int ret;

    if (!dec || !dec->list || !dec->dir_path)
        return -1;

    while (1) {
        mutex_lock(&dec->lock);
        dec->pause = 1;
        if (dec->decoding) {
            mutex_unlock(&dec->lock);
            semaphore_wait(&dec->done);
            continue;
        }
        break;
    }

    node = _lv_ll_get_head(dec->list);
    while (node) {
        next = _lv_ll_get_next(dec->list, node);
        lv_img_dsc_t *dsc = ilv_file_list_get_user_data(dec->list, node);
        if (dsc && dsc->data)
            iscan_decode_clear_img_dsc(dsc);
        node = next;
    }

    _lv_ll_clear(dec->list);
    dec->center = NULL;

    ret = ilv_file_list_init(dec->list, dec->dir_path, iscan_ext_name, 3, sizeof(lv_img_dsc_t));

    dec->pause = 0;
    mutex_unlock(&dec->lock);

    iscan_decode_notify(dec);
    return ret;
}
