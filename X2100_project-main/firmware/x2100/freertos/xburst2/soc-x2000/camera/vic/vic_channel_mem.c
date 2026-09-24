/*
 * Copyright (C) 2020 Ingenic Semiconductor Co., Ltd.
 *
 * Camera Driver for the Ingenic VIC controller
 *
 */


#include <common.h>
#include <list.h>
#include <os.h>
#include <bit_field.h>
#include <wake_lock.h>
#include <driver/irq.h>
#include <driver/cache.h>
// #include <driver/camera.h>
#include <malloc.h>
#include <errno.h>

#include "../hal/camera_gpio.h"
#include "../hal/csi.h"
#include "../hal/vic.h"


#define VIC_ALIGN_SIZE                  8

struct jz_vic_mem_data {
    int index;
    int is_enable;
    int is_finish;

    int irq;
    const char *irq_name;

    critical_thread_cond_t cond;

    void *mem;
    unsigned int mem_cnt;
    unsigned int frm_size;
    unsigned int uv_data_offset;

    volatile unsigned int frame_counter;
    volatile unsigned int wait_timeout;
    unsigned int dma_index;

    /* Camera Device */
    unsigned int cam_mem_cnt;       /* 循环buff个数 */
    struct camera_device camera;
    struct list_head free_list;
    struct list_head usable_list;

    struct frame_data *t[2];
    struct frame_data *frames;

    struct mutex lock;
    struct spinlock spinlock;
    struct wake_lock wake_lock;

    unsigned int vic_frd_c;     /* frame done cnt */
    unsigned int vic_fre_c;     /* frame err cnt */
    unsigned int vic_frov_c;    /* frame overflow cnt */

    unsigned int vic_frm_done;  /* frame done cnt */
    unsigned int vic_frm_output;/* frame output(dqbuf) */

};

static struct jz_vic_mem_data jz_vic_mem_dev[2] = {
    {
        .index                  = 0,
        .is_enable              = 0,
        .irq                    = IRQ_VIC0, /* BASE + 19 */
        .irq_name               = "VIC0",
        .cam_mem_cnt            = 2,
    },

    {
        .index                  = 1,
        .is_enable              = 0,
        .irq                    = IRQ_VIC1, /* BASE + 18 */
        .irq_name               = "VIC1",
        .cam_mem_cnt            = 2,
    },
};


static unsigned int vic_dma_addr[][2] = {
    {VIC_DMA_Y_CH_BUF0_ADDR, VIC_DMA_UV_CH_BUF0_ADDR},
    {VIC_DMA_Y_CH_BUF1_ADDR, VIC_DMA_UV_CH_BUF1_ADDR},
    {VIC_DMA_Y_CH_BUF2_ADDR, VIC_DMA_UV_CH_BUF2_ADDR},
    {VIC_DMA_Y_CH_BUF3_ADDR, VIC_DMA_UV_CH_BUF3_ADDR},
    {VIC_DMA_Y_CH_BUF4_ADDR, VIC_DMA_UV_CH_BUF4_ADDR},
};


static void vic_set_dma_addr(int index, unsigned long y_addr, unsigned long uv_addr, int frame_index)
{
    vic_write_reg(index, vic_dma_addr[frame_index][0], y_addr);
    vic_write_reg(index, vic_dma_addr[frame_index][1], uv_addr);
}

static inline void vic_get_dma_addr(unsigned int index, unsigned long *y_addr, unsigned long *uv_addr, int frame_index)
{
    *y_addr = vic_read_reg(index, vic_dma_addr[frame_index][0]);
    *uv_addr = vic_read_reg(index, vic_dma_addr[frame_index][1]);
}

static void set_dma_addr(int index, struct frame_data *frm, int frame_index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];

    frm->status = frame_status_trans;
    unsigned long address = virt_to_phys(frm->addr);
    vic_set_dma_addr(index, address, address + drv->uv_data_offset, frame_index);
}

static void add_to_free_list(int index, struct frame_data *frm)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];

    frm->status = frame_status_free;
    list_add_tail(&frm->link, &drv->free_list);
}

static struct frame_data *get_free_frm(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];

    if (list_empty(&drv->free_list))
        return NULL;

    struct frame_data *frm = list_first_entry(&drv->free_list, struct frame_data, link);
    list_del_init(&frm->link);

    return frm;
}

static void add_to_usable_list(int index, struct frame_data *frm)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];

    frm->status = frame_status_usable;
    drv->frame_counter++;
    list_add_tail(&frm->link, &drv->usable_list);
}

static struct frame_data *get_usable_frm(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];

    if (list_empty(&drv->usable_list))
        return NULL;

    drv->frame_counter--;

    struct frame_data *frm = list_first_entry(&drv->usable_list, struct frame_data, link);
    list_del_init(&frm->link);

    return frm;
}

static void init_frm_lists(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];

    INIT_LIST_HEAD(&drv->free_list);
    INIT_LIST_HEAD(&drv->usable_list);
    drv->dma_index = 0;
    drv->frame_counter = 0;
    drv->wait_timeout = 0;

    int i;
    for (i = 0; i < drv->mem_cnt; i++) {
        struct frame_data *frm = &drv->frames[i];
        memset(frm, 0x00, sizeof(struct frame_data));
        frm->addr = drv->mem + i * drv->frm_size;
        frm->info.index = i;
        frm->info.width = drv->camera.sensor->info.width;
        frm->info.height = drv->camera.sensor->info.height;
        frm->info.pixfmt = drv->camera.sensor->info.data_fmt;
        frm->info.size = drv->frm_size;
        frm->info.vaddr = frm->addr;
        frm->info.paddr = virt_to_phys(frm->addr);
        add_to_free_list(index, frm);
    }
}

static void reset_frm_lists(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    int i;

    for (i = 0; i < drv->mem_cnt; i++) {
        struct frame_data *frm = &drv->frames[i];
        if (drv->t[0] != frm && drv->t[1] != frm)
            list_del_init(&frm->link);
        add_to_free_list(index, frm);
    }

    drv->dma_index = 0;
    drv->frame_counter = 0;
    drv->wait_timeout = 0;
}

static void init_dma_addr(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    struct frame_data *frm;

    frm = get_free_frm(index);
    drv->t[0] = frm;
    set_dma_addr(index, frm, 0);

    frm = get_free_frm(index);
    if (!frm)
        frm = drv->t[0];

    drv->t[1] = frm;
    set_dma_addr(index, frm, 1);
}


static void vic_irq_dma_handler(int irq, void *data)
{
    struct jz_vic_mem_data *drv = (struct jz_vic_mem_data *)data;
    int index = drv->index;
    volatile unsigned long state, pending, mask;

    mask = vic_read_reg(index, VIC_INT_MASK);
    state = vic_read_reg(index, VIC_INT_STA);
    pending = state & (~mask);
    vic_write_reg(index, VIC_INT_CLR, pending);

    if (get_bit_field_v(&pending, DMA_FRD)) {
        struct frame_data *frm, *usable_frm = NULL;
        int frame_index = drv->dma_index;
        drv->dma_index = !frame_index;

        drv->vic_frm_done++;
        /*
         * 当传输列表只有一帧的时候，不能用这一帧
         */
        if (drv->t[0] != drv->t[1])
            usable_frm = drv->t[frame_index];

        /*
         * 1 优先从空闲列表中获取新的帧加入传输
         * 2 如果空闲列表没有帧，那么传输完成列表中获取
         * 3 如果完成列表也没有，那么用下一帧做保底
         */
        frm = get_free_frm(index);
        if (!frm)
            frm = get_usable_frm(index);
        if (!frm)
            frm = drv->t[!frame_index];
        drv->t[frame_index] = frm;
        set_dma_addr(index, frm, frame_index);

        if (usable_frm) {
            usable_frm->info.sequence = drv->vic_frm_done;
            usable_frm->info.timestamp = get_time_us();
            add_to_usable_list(index, usable_frm);
            critical_thread_cond_signal(&drv->cond);
        }
    }

    if (get_bit_field_v(&pending, VIC_FRM_START)) {
        struct frame_data *current_frm = drv->t[drv->dma_index];
        if(drv->camera.sensor->ops.frame_start_callback){
            if(drv->camera.sensor->ops.frame_start_callback() >= 1) {
                current_frm->info.shutter_count = drv->camera.sensor->ops.frame_start_callback();
            }
        } else {
            current_frm->info.shutter_count = 0;
        }
    }

    if (get_bit_field_v(&pending, VIC_HVRES_ERR)) {
        drv->vic_fre_c++;
        printf("## VIC WARN status = 0x%08lx\n", pending);
    }
    if (get_bit_field_v(&pending, VIC_FIFO_OVF)) {
        drv->vic_frov_c++;
    }
    if (get_bit_field_v(&pending, VIC_FRD)) {
        drv->vic_frd_c++;
    }

    return ;
}

static inline void *m_dma_alloc_coherent(int size)
{
    return memalign(4096, ALIGN(size, cache_line_size()));
}

static inline void m_dma_free_coherent(void *mem, int size)
{
    free(mem);
}

static inline void m_cache_sync(void *mem, int size)
{
    invalidate_dcache_force((unsigned long) mem, size);
}

static int vic_alloc_mem(int index, struct sensor_attr *attr)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    int mem_cnt = drv->cam_mem_cnt + 1;
    int frame_size, uv_data_offset;
    int line_length;
    int frame_align_size;
    int alloc_width;
    int alloc_height;

    assert(mem_cnt >= 2);

    if ( (attr->dbus_type == SENSOR_DATA_BUS_MIPI ) && (attr->mipi.mipi_crop.enable) ){
        alloc_width = attr->mipi.mipi_crop.output_width;
        alloc_height = attr->mipi.mipi_crop.output_height;
    } else {
        alloc_width = attr->sensor_info.width;
        alloc_height = attr->sensor_info.height;
    }

    if (camera_fmt_is_NV12(attr->info.data_fmt) ) {
        line_length = ALIGN(alloc_width, 16);
        frame_size = line_length * alloc_height;
        if (frame_size % VIC_ALIGN_SIZE) {
            printf("frm_size not aligned\n");
            return -EINVAL;
        }
        uv_data_offset = frame_size;
        frame_size += frame_size / 2;

    } else if (is_output_y8(index, attr) || camera_fmt_is_8BIT(attr->info.data_fmt)){
        line_length = ALIGN(alloc_width, 8);
        frame_size = line_length * alloc_height;
        if (frame_size % VIC_ALIGN_SIZE) {
            printf("frm_size not aligned\n");
            return -EINVAL;
        }
        uv_data_offset = 0;

    } else {
        line_length = ALIGN(alloc_width, 8) * 2;
        frame_size = line_length * alloc_height;
        if (frame_size % VIC_ALIGN_SIZE) {
            printf("frm_size not aligned\n");
            return -EINVAL;
        }
        uv_data_offset = 0;
    }

    frame_align_size = ALIGN(frame_size, 4096);
    drv->uv_data_offset = uv_data_offset;

    drv->mem = m_dma_alloc_coherent(frame_align_size * mem_cnt);
    if (drv->mem == NULL) {
        printf("vic%d : camera failed to alloc mem: %u\n", index, frame_align_size * mem_cnt);
        return -ENOMEM;
    }

    drv->mem_cnt = mem_cnt;
    drv->frm_size = frame_align_size;
    attr->info.fps = (attr->sensor_info.fps >> 16) / (attr->sensor_info.fps & 0xFFFF);
    attr->info.line_length = line_length;
    attr->info.phys_mem = virt_to_phys(drv->mem);
    attr->info.frame_size = frame_size;
    attr->info.frame_align_size = frame_align_size;
    attr->info.frame_nums = mem_cnt;

    drv->frames = malloc(drv->mem_cnt * sizeof(drv->frames[0]));
    assert(drv->frames);
    init_frm_lists(index);

    return 0;
}

static void vic_free_mem(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];

    m_dma_free_coherent(drv->mem, drv->mem_cnt * drv->frm_size);
    free(drv->frames);
    drv->mem = NULL;
    drv->frames = NULL;
}


static int vic_mem_stream_on(int index, struct sensor_attr *attr)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    int ret = 0;

    init_dma_addr(index);

    ret = vic_stream_on(index, attr);
    if (ret) {
        printf("vic%d(mem) : vic stream on failed\n", index);
        return ret;
    }

    enable_irq(drv->irq);

    drv->camera.is_stream_on = 1;

    return 0;
}

static void vic_mem_stream_off(int index, struct sensor_attr *attr)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];

    disable_irq(drv->irq);

    vic_stream_off(index, attr);

    drv->camera.is_stream_on = 0;
}

static int vic_mem_power_on(int index)
{
    return vic_power_on(index);
}

static void vic_mem_power_off(int index)
{
    vic_power_off(index);
}

static int vic_mem_check_frame_mem(int index, void *mem)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    unsigned int size = mem - drv->mem;

    if (mem < drv->mem)
        return -1;

    if (size % drv->frm_size)
        return -1;

    if (size / drv->frm_size >= drv->mem_cnt)
        return -1;

    return 0;
}

static inline struct frame_data *vic_mem_2_frame_data(int index, void *mem)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    unsigned int size = mem - drv->mem;
    unsigned int count = size / drv->frm_size;

    assert(!(size % drv->frm_size));
    assert(count < drv->mem_cnt);

    return &drv->frames[count];
}

static int vic_mem_check_frame_info(int index, struct frame_info *info)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    struct frame_data *frm = &drv->frames[info->index];

    if (info->index >= drv->mem_cnt)
        return -1;

    if (frm->info.paddr != info->paddr)
        return -1;

    return 0;
}

static inline struct frame_data *vic_mem_frame_info_2_frame_data(int index, struct frame_info *info)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];

    if (!vic_mem_check_frame_info(index, info))
        return &drv->frames[info->index];
    else {
        //camera_dump_frame_info(info);
        return NULL;
    }
}

static void vic_mem_put_frame(int index, struct frame_data *frm)
{

    os_enter_critical();

    if (frm->status != frame_status_user) {
        printf("vid%d camera double free of vic frame index %u\n", index, frm->info.index);
    } else {
        list_del_init(&frm->link);
        add_to_free_list(index, frm);
    }

    os_exit_critical();
}

camera_frame_error_type soc_vic_chan_mem_get_frame_error(int index);
static int vic_mem_get_frame(int index, struct frame_data **frame, unsigned int timeout_ms)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    struct camera_device *camera = &drv->camera;
    struct frame_data *frm;
    int ret = -EAGAIN;
    camera_frame_error_type err;

    os_enter_critical();

    err = soc_vic_chan_mem_get_frame_error(index);
    if (err != camera_error_null)
        goto unlock;

    frm = get_usable_frm(index);

    if (!frm && timeout_ms) {
        ret = critical_thread_cond_wait_timeout(&drv->cond, timeout_ms);
        if (ret) {
            if (camera->is_stream_on)
                drv->wait_timeout = 1;
            goto unlock;
        }

        frm = get_usable_frm(index);
        if (ret <= 0 && !frm) {
            ret = -ETIMEDOUT;
            printf("camera: wait frame time out\n");
        }
    }

    if (frm) {
        ret = 0;
        frm->status = frame_status_user;
        *frame = frm;
    }

unlock:
    os_exit_critical();

    return ret;
}


/******************************************************************************
 *
 * 应用调用 函数实现
 *
 *****************************************************************************/
int soc_vic_chan_mem_detect(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    int ret = -ENODEV;

    mutex_lock(&drv->lock);

    if (!drv->camera.sensor) {
        printf("vic%d: no sensor regisered\n", index);
        goto unlock;
    }

    if (drv->camera.is_power_on) {
        ret = 0;
        goto unlock;
    }

    if (!vic_mem_power_on(index)) {
        drv->camera.is_power_on = 1;
        ret = 0;
    }

unlock:
    mutex_unlock(&drv->lock);

    return ret;
}

struct camera_info *soc_vic_chan_mem_get_info(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    struct camera_device *camera = &drv->camera;

    return &camera->sensor->info;
}

camera_frame_error_type soc_vic_chan_mem_get_frame_error(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    struct camera_device *camera = &drv->camera;

    if (drv->wait_timeout)
        return camera_error_timeout;

    if (!camera->is_stream_on)
        return camera_error_stream_is_off;

    return camera_error_null;
}

int soc_vic_chan_mem_stream_on(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    int ret = 0;

    mutex_lock(&drv->lock);

    if (!drv->camera.is_power_on) {
        printf("vic%d : camera can't stream on when not power on\n", index);
        ret = -EINVAL;
        goto unlock;
    }

    if (drv->camera.is_stream_on)
        goto unlock;

    reset_frm_lists(index);

    ret = vic_mem_stream_on(index, drv->camera.sensor);

unlock:
    mutex_unlock(&drv->lock);

    return ret;
}


void soc_vic_chan_mem_stream_off(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];

    mutex_lock(&drv->lock);

    if (drv->camera.is_stream_on)
        vic_mem_stream_off(index, drv->camera.sensor);

    mutex_unlock(&drv->lock);
}


int soc_vic_chan_mem_power_on(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    int ret = 0;

    mutex_lock(&drv->lock);

    wake_lock(&drv->wake_lock);

    if (!drv->camera.is_power_on) {
        ret = vic_mem_power_on(index);
        if (!ret)
            drv->camera.is_power_on = 1;
        else
            wake_unlock(&drv->wake_lock);
    }

    mutex_unlock(&drv->lock);

    return ret;
}


void soc_vic_chan_mem_power_off(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];

    mutex_lock(&drv->lock);

    if (drv->camera.is_stream_on)
        vic_mem_stream_off(index, drv->camera.sensor);

    if (drv->camera.is_power_on) {
        vic_mem_power_off(index);
        drv->camera.is_power_on = 0;
    }

    wake_unlock(&drv->wake_lock);

    mutex_unlock(&drv->lock);
}


void *soc_vic_chan_mem_get_frame(int index)
{
    struct frame_data *frm;
    int ret;

    ret = vic_mem_get_frame(index, &frm, 0);
    if (0 == ret) {
        m_cache_sync(frm->addr, frm->info.size);
    } else
        return NULL;

    return frm->info.vaddr;
}

void *soc_vic_chan_mem_wait_frame(int index)
{
    struct frame_data *frm;
    int ret;

    ret = vic_mem_get_frame(index, &frm, 3000);
    if (0 == ret) {
        m_cache_sync(frm->addr, frm->info.size);
    } else
        return NULL;

    return frm->info.vaddr;
}

int soc_vic_chan_mem_put_frame(int index, void *buf)
{
    struct frame_data *frm;

    if (vic_mem_check_frame_mem(index, buf))
        return -EINVAL;

    frm = vic_mem_2_frame_data(index, buf);
    m_cache_sync(frm->addr, frm->info.size);
    vic_mem_put_frame(index, frm);

    return 0;
}


int soc_vic_chan_mem_dqbuf(int index, struct frame_info *frame)
{
    struct frame_data *frm;
    int ret;

    assert(frame);

    ret = vic_mem_get_frame(index, &frm, 0);
    if (0 == ret) {
        m_cache_sync(frm->addr, frm->info.size);
    } else
        return ret;

    memcpy(frame, &frm->info, sizeof(struct frame_info));

    return 0;
}

int soc_vic_chan_mem_dqbuf_wait(int index, struct frame_info *frame)
{
    struct frame_data *frm;
    int ret;

    assert(frame);

    ret = vic_mem_get_frame(index, &frm, 3000);
    if (0 == ret) {
        m_cache_sync(frm->addr, frm->info.size);
    } else
        return ret;

    memcpy(frame, &frm->info, sizeof(struct frame_info));

    return 0;
}

int soc_vic_chan_mem_qbuf(int index, struct frame_info *frame)
{
    struct frame_data *frm;

    assert(frame);

    frm = vic_mem_frame_info_2_frame_data(index, frame);
    if (!frm) {
        printf("vic_mem_frame_info_2_frame_data fail\n");
        return -EINVAL;
    }
    m_cache_sync(frm->addr, frm->info.size);
    vic_mem_put_frame(index, frm);

    return 0;
}

int soc_vic_chan_set_hal_sensor_reg(int index,struct sensor_dbg_register *reg)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    struct sensor_attr *sensor = drv->camera.sensor;
    int ret;

    if (sensor->ops.set_register) {
        ret = sensor->ops.set_register(reg);
        if (ret < 0) {
           printf("%s set_register fail\n", __func__);
            return ret;
        }
    } else {
       printf("sensor->ops.set_register is NULL!\n");
        return -EINVAL;
    }

    return 0;
}

int soc_vic_chan_get_hal_sensor_reg(int index,struct sensor_dbg_register *reg)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    struct sensor_attr *sensor = drv->camera.sensor;
    int ret;

    if (sensor->ops.get_register) {
        ret = sensor->ops.get_register(reg);
        if (ret < 0) {
           printf("%s get_register fail\n", __func__);
            return ret;
        }
    } else {
       printf("sensor->ops.get_register is NULL!\n");
        return -EINVAL;
    }

    return 0;
}

unsigned int soc_vic_chan_mem_get_available_frame_count(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];

    return drv->frame_counter;
}

void soc_vic_chan_mem_skip_frames(int index, unsigned int frames)
{
    os_enter_critical();

    while (frames--) {
        struct frame_data *frm = get_usable_frm(index);
        if (frm == NULL)
            break;
        add_to_free_list(index, frm);
    }

    os_exit_critical();
}


#define error_if(_cond)                                                 \
    do {                                                                \
        if (_cond) {                                                    \
            printf("vic(mem): failed to check: %s\n", #_cond);   \
            ret = -1;                                                   \
            goto unlock;                                                \
        }                                                               \
    } while (0)



/*
 * 格式信息转换 转换后的格式经过VIC DMA 到DDR
 * sensor format : Sensor格式在sensor driver中根据setting指定
 * camera format : Camera格式在camera driver中使用,并暴露给应用
 *
 */
struct format_mapping {
    sensor_pixel_fmt sensor_fmt; /* sensor格式 */
    camera_pixel_fmt camera_fmt; /* Camera格式(VIC扩展为16bit) */
};

static struct format_mapping sensor_camera_format_map[] = {
    {SENSOR_PIXEL_FMT_Y8_1X8,           CAMERA_PIX_FMT_GREY},   /* 其他条件必须满足 8bit要求 */
    {SENSOR_PIXEL_FMT_UYVY8_2X8,        CAMERA_PIX_FMT_UYVY},
    {SENSOR_PIXEL_FMT_VYUY8_2X8,        CAMERA_PIX_FMT_VYUY},
    {SENSOR_PIXEL_FMT_YUYV8_2X8,        CAMERA_PIX_FMT_YUYV},
    {SENSOR_PIXEL_FMT_YVYU8_2X8,        CAMERA_PIX_FMT_YVYU},

    {SENSOR_PIXEL_FMT_SBGGR8_1X8,       CAMERA_PIX_FMT_SBGGR8}, /* 其他条件必须满足 8bit要求 */
    {SENSOR_PIXEL_FMT_SGBRG8_1X8,       CAMERA_PIX_FMT_SGBRG8}, /* 其他条件必须满足 8bit要求 */
    {SENSOR_PIXEL_FMT_SGRBG8_1X8,       CAMERA_PIX_FMT_SGRBG8}, /* 其他条件必须满足 8bit要求 */
    {SENSOR_PIXEL_FMT_SRGGB8_1X8,       CAMERA_PIX_FMT_SRGGB8}, /* 其他条件必须满足 8bit要求 */
    {SENSOR_PIXEL_FMT_SBGGR10_1X10,     CAMERA_PIX_FMT_SBGGR16},
    {SENSOR_PIXEL_FMT_SGBRG10_1X10,     CAMERA_PIX_FMT_SGBRG16},
    {SENSOR_PIXEL_FMT_SGRBG10_1X10,     CAMERA_PIX_FMT_SGRBG16},
    {SENSOR_PIXEL_FMT_SRGGB10_1X10,     CAMERA_PIX_FMT_SRGGB16},
    {SENSOR_PIXEL_FMT_SBGGR12_1X12,     CAMERA_PIX_FMT_SBGGR16},
    {SENSOR_PIXEL_FMT_SGBRG12_1X12,     CAMERA_PIX_FMT_SGBRG16},
    {SENSOR_PIXEL_FMT_SGRBG12_1X12,     CAMERA_PIX_FMT_SGRBG16},
    {SENSOR_PIXEL_FMT_SRGGB12_1X12,     CAMERA_PIX_FMT_SRGGB16},
};

static int sensor_attribute_check_init(int index, struct sensor_attr *sensor)
{
    int ret = -EINVAL;

    error_if(!sensor->device_name);
    error_if(sensor->sensor_info.width < 64 || sensor->sensor_info.width >= 3840);
    error_if(sensor->sensor_info.height < 64 || sensor->sensor_info.height >= 2560);
    error_if(!sensor->ops.power_on);
    error_if(!sensor->ops.power_off);
    error_if(!sensor->ops.stream_on);
    error_if(!sensor->ops.stream_off);

    memset(sensor->info.name, 0x00, sizeof(sensor->info.name));
    memcpy(sensor->info.name, sensor->device_name, strlen(sensor->device_name));

    sensor->info.width =  sensor->sensor_info.width;
    sensor->info.height =  sensor->sensor_info.height;

    int i = 0;
    int size = ARRAY_SIZE(sensor_camera_format_map);
    for (i = 0; i < size; i++) {
        if (sensor->sensor_info.fmt == sensor_camera_format_map[i].sensor_fmt)
            break;
    }

    if (i >= size) {
        printf("attribute check: sensor data_fmt(0x%x) is NOT support.\n", sensor->sensor_info.fmt);
        goto unlock;
    }

    /* 当sensor输出为8BIT时, 其他控制条件必须同时满足为8bit */
    if (sensor_fmt_is_8BIT(sensor->sensor_info.fmt)
        && !is_output_y8(index, sensor)) {
        if (sensor->dbus_type == SENSOR_DATA_BUS_DVP) {
            printf("Please check sensor format and VIC data format\n");
            printf("sensor format is 8BIT(0x%x)\n", sensor->sensor_info.fmt);
            printf("VIC inteface(DVP) data format is not 8BIT(0x%x)\n", sensor->dvp.data_fmt);
            goto unlock;
        }

        if (sensor->dbus_type == SENSOR_DATA_BUS_MIPI) {
            printf("Please check sensor format and VIC data format\n");
            printf("sensor format is 8BIT(0x%x)\n", sensor->sensor_info.fmt);
            printf("VIC inteface(MIPI) data format is not 8BIT(0x%x)\n", sensor->mipi.data_fmt);
            goto unlock;
        }

        printf("Now dbus type(%d) not support\n", sensor->dbus_type);
        goto unlock;
    }

    camera_pixel_fmt data_fmt = sensor_camera_format_map[i].camera_fmt;

    /* 当sensor输出格式为YUV422时,可通过VIC DMA重新排列输出格式 */
    if (is_output_yuv422(index, sensor) && camera_fmt_is_YUV422(data_fmt)) {
        switch (sensor->dma_mode) {
        case SENSOR_DATA_DMA_MODE_NV12:
            data_fmt = CAMERA_PIX_FMT_NV12;
            break;
        case SENSOR_DATA_DMA_MODE_NV21:
            data_fmt = CAMERA_PIX_FMT_NV21;
            break;
        case SENSOR_DATA_DMA_MODE_GREY:
            data_fmt = CAMERA_PIX_FMT_GREY;
            break;
        default:
            break;
        }
    }

    sensor->info.data_fmt = data_fmt;

    if (sensor->dbus_type == SENSOR_DATA_BUS_DVP) {
        switch(sensor->dvp.gpio_mode) {
        case DVP_PA_LOW_10BIT:
        case DVP_PA_HIGH_10BIT:
            if (sensor->dvp.data_fmt > DVP_RAW10) {
                printf("attribute check: data_fmt set error,should be less than DVP_RAW12.\n");
                goto unlock;
            }
            break;

        case DVP_PA_12BIT:
            break;

        case DVP_PA_LOW_8BIT:
        case DVP_PA_HIGH_8BIT:
            if (sensor->dvp.data_fmt < DVP_YUV422) {
                if (sensor->dvp.data_fmt > DVP_RAW8) {
                    printf("attribute check: data_fmt set error,should be DVP_RAW8.\n");
                    goto unlock;
                }
            }
            break;

        default:
            printf("attribute check: Unsupported this format.\n");
            goto unlock;
        }
    }
    return 0;

unlock:
    return ret;
}

int soc_vic_register_sensor_mem_routine(int index, int mem_cnt, struct sensor_attr *sensor)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];
    int ret = 0;

    assert(drv->is_finish > 0);
    assert(!drv->camera.sensor);

    ret = sensor_attribute_check_init(index, sensor);
    assert(ret == 0);

    mutex_lock(&drv->lock);

    drv->cam_mem_cnt = mem_cnt;
    if (drv->cam_mem_cnt < 1) {
        drv->cam_mem_cnt = 2;
    }

    drv->camera.sensor = sensor;
    ret = vic_alloc_mem(index, sensor);
    if (ret) {
        drv->camera.sensor = NULL;
        goto unlock;
    }

    drv->camera.sensor = sensor;
    drv->camera.is_power_on = 0;
    drv->camera.is_stream_on = 0;

unlock:
    mutex_unlock(&drv->lock);

    return ret;
}

void soc_vic_unregister_sensor_mem_routine(int index, struct sensor_attr *sensor)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];

    assert(drv->is_finish > 0);
    assert(drv->camera.sensor);
    assert(sensor == drv->camera.sensor);

    mutex_lock(&drv->lock);

    if (drv->camera.is_stream_on)
        panic("vic%d(mem) : failed to unregister, when camera stream on!\n", index);

    if (drv->camera.is_power_on) {
        struct sensor_attr *attr = drv->camera.sensor;
        attr->ops.power_off();

        drv->camera.is_power_on = 0;
    }

    drv->camera.sensor = NULL;

    vic_free_mem(index);

    mutex_unlock(&drv->lock);
}


int jz_vic_mem_drv_init(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];

    critical_thread_cond_init(&drv->cond);

    mutex_init(&drv->lock);
    spin_lock_init(&drv->spinlock);
    wake_lock_init(&drv->wake_lock, "vic_mem_wake_lock");

    request_irq_disabled(drv->irq, 0, vic_irq_dma_handler, drv->irq_name, drv);

    drv->is_finish = 1;

    printf("vic%d(mem) register successfully\n", index);
    return 0;
}

void jz_vic_mem_drv_deinit(int index)
{
    struct jz_vic_mem_data *drv = &jz_vic_mem_dev[index];

    if (!drv->is_finish)
        return ;

    drv->is_finish = 0;

    release_irq(drv->irq);
}
