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
#include <driver/clk.h>
#include <driver/irq.h>
#include <driver/cache.h>
#include <driver/camera.h>

#include "../hal/camera_gpio.h"
#include "../hal/csi.h"
#include "../hal/vic.h"

#include "cim_regs.h"


#define CIM_DMA_DESC_COUNT              2  /* 不修正为其他值 */
#define MEM_ALIGN(x, n)                 (((x) + (n) - 1) - ((x) + (n) - 1) % (n))
/*
 * DMA description 描述
 */
typedef union desc_intc {
    unsigned int d32;
    struct {
        unsigned int reserved0:1;
        unsigned int eof_mask:1;
        unsigned int sof_mask:1;
        unsigned int reserved3_31:29;
    } data;
} desc_intc_t;

typedef union desc_cfg {
    unsigned int d32;
    struct {
        unsigned int desc_end:1;    /* =0: not the last one
                                     * =1: this desc is the last one of the chain */
        unsigned int reserved1_15:15;
        unsigned int write_back_format:3;
        unsigned int reserved19_25:7;
        unsigned int id:6;
    } data;
} desc_cfg_t;

typedef union desc_hist_cfg {
    unsigned int d32;
    struct {
        unsigned int gain_mul:8;
        unsigned int gain_add:8;
        unsigned int reserved16_30:15;
        unsigned int hist_en:1;
    } data;
} desc_hist_cfg_t;

struct frame_desc {
    unsigned long next_desc_addr;
    unsigned long write_back_address;
    unsigned long write_back_stride;
    desc_intc_t desc_intc_t;
    desc_cfg_t  desc_cfg_t;
    desc_hist_cfg_t desc_hist_cfg_t;
    unsigned long hist_write_back_addr;
    unsigned long sf_write_back_addr;
} __attribute__ ((aligned (8)));

struct jz_cim_data {
    int is_enable;
    int is_finish;

    int irq;
    const char *irq_name;

    struct mutex lock;
    struct spinlock spinlock;
    struct wake_lock wake_lock;

    struct clk *cim_gate_clk;
    const char *cim_gate_clk_name;  /* CIM */

    critical_thread_cond_t cond;

    void *mem;
    unsigned int mem_cnt;   /* 申请循环buff个数 */
    unsigned int frame_size;
    struct frame_desc *frame_descs;

    volatile unsigned int frame_counter;
    volatile unsigned int free_frame_counter;
    unsigned int wait_timeout;

    /* Camera Device */
    char *device_name;              /* 设备节点名字 */
    unsigned int cam_mem_cnt;       /* 应用传递循环buff个数 */
    struct camera_device camera;

    struct list_head free_list;
    struct list_head usable_list;
    struct frame_data *frames;
    struct frame_data **trans_list;

    unsigned int cim_frm_done;  /* frame done cnt */
    unsigned int cim_frm_output;/* frame output(dqbuf) */
};

static int is_frame_size_check = 1;
static int is_enable_snapshot = 0;

static struct jz_cim_data jz_cim_dev = {
    .is_enable              = 0,
    .irq                    = IRQ_CIM, /* BASE + 30 */
    .irq_name               = "CIM",
    .cim_gate_clk_name      = "gate_cim",
    .cam_mem_cnt            = 2,
};

#define CIM_ADDR(reg)    ((volatile unsigned long *)CKSEG1ADDR(CIM_IOBASE + reg))

static inline void cim_write_reg(unsigned int reg, unsigned int val)
{
    *CIM_ADDR(reg) = val;
}

static inline unsigned int cim_read_reg(unsigned int reg)
{
    return *CIM_ADDR(reg);
}

static inline void cim_set_bit(unsigned int reg, unsigned int start, unsigned int end, unsigned int val)
{
    set_bit_field_v(CIM_ADDR(reg), start, end, val);
}

static inline unsigned int cim_get_bit(unsigned int reg, unsigned int start, unsigned int end)
{
    return get_bit_field_v(CIM_ADDR(reg), start, end);
}

static inline void cim_dump_regs(void)
{
    printf("Dump CIM regs\n");
    printf("GLB_CFG                  :0x%08x\n", cim_read_reg(CIM_GLB_CFG));
    printf("CROP_SIZE                :0x%08x\n", cim_read_reg(CIM_CROP_SIZE));
    printf("CROP_SITE                :0x%08x\n", cim_read_reg(CIM_CROP_SITE));
    printf("RESIZE_CFG               :0x%08x\n", cim_read_reg(CIM_RESIZE_CFG));
    printf("RESIZE_COEF_X            :0x%08x\n", cim_read_reg(CIM_RESIZE_COEF_X));
    printf("RESIZE_COEF_Y            :0x%08x\n", cim_read_reg(CIM_RESIZE_COEF_Y));
    printf("SCAN_CFG                 :0x%08x\n", cim_read_reg(CIM_SCAN_CFG));
    printf("DLY_CFG                  :0x%08x\n", cim_read_reg(CIM_DLY_CFG));
    printf("QOS_CTRL                 :0x%08x\n", cim_read_reg(CIM_QOS_CTRL));
    printf("QOS_CFG                  :0x%08x\n", cim_read_reg(CIM_QOS_CFG));
    printf("DES_ADDR                 :0x%08x\n", cim_read_reg(CIM_DES_ADDR));
    printf("CTRL                     :0x%08x\n", cim_read_reg(CIM_CTRL));
    printf("ST                       :0x%08x\n", cim_read_reg(CIM_ST));
    printf("CLR_ST                   :0x%08x\n", cim_read_reg(CIM_CLR_ST));
    printf("INTC                     :0x%08x\n", cim_read_reg(CIM_INTC));
    printf("INT_FLAG                 :0x%08x\n", cim_read_reg(CIM_INT_FLAG));
    printf("FRM_ID                   :0x%08x\n", cim_read_reg(CIM_FRM_ID));
    printf("ACT_SIZE                 :0x%08x\n", cim_read_reg(CIM_ACT_SIZE));
    printf("DBG_CGC                  :0x%08x\n", cim_read_reg(CIM_DBG_CGC));
}

static inline void cim_dump_debug_desc(void)
{
    printf("Dump CIM debug DMA desc\n");
    printf("DBG next_desc_addr       :0x%08x\n", cim_read_reg(CIM_DBG_DES));
    printf("DBG write_back_address   :0x%08x\n", cim_read_reg(CIM_DBG_DES));
    printf("DBG write_back_stride    :0x%08x\n", cim_read_reg(CIM_DBG_DES));
    printf("DBG desc_intc_t          :0x%08x\n", cim_read_reg(CIM_DBG_DES));
    printf("DBG desc_cfg_t           :0x%08x\n", cim_read_reg(CIM_DBG_DES));
    printf("DBG desc_hist_cfg_t      :0x%08x\n", cim_read_reg(CIM_DBG_DES));
    printf("hist_write_back_addr     :0x%08x\n", cim_read_reg(CIM_DBG_DES));
    printf("sf_write_back_addr       :0x%08x\n", cim_read_reg(CIM_DBG_DES));
    printf("DBG current DMA addr     :0x%08x\n", cim_read_reg(CIM_DBG_DMA));
}

static inline void cim_dump_desc(void)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    struct frame_desc *descs = drv->frame_descs;
    int mem_cnt = CIM_DMA_DESC_COUNT;

    printf("Dump CIM DMA desc\n");
    int i = 0;
    for (i = 0; i < mem_cnt; i++) {
        printf("desc index = %d\n", i);
        printf("next_desc_addr       :0x%08lx\n", descs[i].next_desc_addr);
        printf("write_back_address   :0x%08lx\n", descs[i].write_back_address);
        printf("write_back_stride    :0x%08lx\n", descs[i].write_back_stride);
        printf("desc_intc_t          :0x%08x\n", descs[i].desc_intc_t.d32);
        printf("desc_cfg_t           :0x%08x\n", descs[i].desc_cfg_t.d32);
        printf("desc_hist_cfg_t      :0x%08x\n", descs[i].desc_hist_cfg_t.d32);
        printf("hist_write_back_addr :0x%08lx\n", descs[i].hist_write_back_addr);
        printf("sf_write_back_addr   :0x%08lx\n", descs[i].sf_write_back_addr);
        printf("\n");
    }
}

static void add_to_free_list(struct frame_data *frm)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    frm->status = frame_status_free;
    drv->free_frame_counter++;
    list_add_tail(&frm->link, &drv->free_list);
}

static struct frame_data *get_free_frm(void)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    if (list_empty(&drv->free_list))
        return NULL;

    drv->free_frame_counter--;
    struct frame_data *frm = list_first_entry(&drv->free_list, struct frame_data, link);
    list_del_init(&frm->link);

    return frm;
}

static void add_to_usable_list(struct frame_data *frm)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    frm->status = frame_status_usable;
    drv->frame_counter++;
    list_add_tail(&frm->link, &drv->usable_list);
}

static struct frame_data *get_usable_frm(void)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    if (list_empty(&drv->usable_list))
        return NULL;

    drv->frame_counter--;
    struct frame_data *frm = list_first_entry(&drv->usable_list, struct frame_data, link);
    list_del_init(&frm->link);

    return frm;
}

static void set_desc_addr(int id, struct frame_data *frm)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    struct frame_desc *desc = &drv->frame_descs[id];

    desc->write_back_address = virt_to_phys(frm->addr);
    flush_dcache_force((unsigned long)desc, sizeof(*desc));
}

static void set_transfer_frame(int index, struct frame_data *frm)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    frm->status = frame_status_trans;
    drv->trans_list[index] = frm;
    set_desc_addr(index, frm);
}


static void init_frm_lists(void)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    INIT_LIST_HEAD(&drv->free_list);
    INIT_LIST_HEAD(&drv->usable_list);
    drv->frame_counter = 0;
    drv->free_frame_counter = 0;
    drv->wait_timeout = 0;

    int i;
    for (i = 0; i < drv->mem_cnt; i++) {
        struct frame_data *frm = &drv->frames[i];
        memset(frm, 0x00, sizeof(struct frame_data));
        frm->addr = drv->mem + i * drv->frame_size;
        frm->info.index = i;
        frm->info.width = drv->camera.sensor->info.width;
        frm->info.height = drv->camera.sensor->info.height;
        frm->info.pixfmt = drv->camera.sensor->info.data_fmt;
        frm->info.size = drv->frame_size;
        frm->info.vaddr = frm->addr;
        frm->info.paddr = virt_to_phys(frm->addr);
        add_to_free_list(frm);
    }
}

static void reset_frm_lists(void)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    int i;

    drv->frame_counter = 0;
    drv->free_frame_counter = 0;
    drv->wait_timeout = 0;

    for (i = 0; i < drv->mem_cnt; i++) {
        struct frame_data *frm = &drv->frames[i];
        if (frm->status != frame_status_trans)
            list_del_init(&frm->link);

        add_to_free_list(frm);
    }

}

static void cim_start(void)
{
    cim_set_bit(CIM_CTRL, CTRL_START, 1);
}

static void cim_soft_reset(void)
{
    /* reset cim 控制器 */
    cim_set_bit(CIM_CTRL, CTRL_SOFT_RESET, 1);

    int timeout = 3000;
    while (!cim_get_bit(CIM_ST, ST_SRA)) {
        udelay(100);
        if (--timeout == 0) {
            printf("cim reset wait finish timeout: %x\n", cim_read_reg(CIM_ST));
            break;
        }
    }

    cim_set_bit(CIM_CLR_ST, CLR_ST_SOFT_RESET, 1);
}

static void cim_clear_status_all(void)
{
    /* 清除所有状态标志 */
    cim_write_reg(CIM_CLR_ST, 0xFF);
}

static void cim_set_dma_desc_addr(int index)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    unsigned long des_addr_phy = virt_to_phys(&drv->frame_descs[index]);

    cim_write_reg(CIM_DES_ADDR, des_addr_phy);
}

static void cim_init_dma_desc(void)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    struct frame_data *frm;

    frm = get_free_frm();
    set_transfer_frame(0, frm);

    frm = get_free_frm();
    if (!frm)
        frm = drv->trans_list[0];
    set_transfer_frame(1, frm);
}

static void cim_irq_handler(int irq, void *data)
{
    struct jz_cim_data *drv = (struct jz_cim_data *)data;
    unsigned long status;
    static unsigned int shutter_count;

    status = cim_read_reg(CIM_INT_FLAG);

    if (get_bit_field(&status, INT_FLAG_EOW) || get_bit_field(&status, INT_FLAG_EOF)) {
        cim_set_bit(CIM_CLR_ST, CLR_ST_EOW, 1);
        cim_set_bit(CIM_CLR_ST, CLR_ST_EOF, 1);

        struct frame_data *frm;
        struct frame_data *usable_frm = NULL;
        int index = cim_read_reg(CIM_FRM_ID); /* 当前已传输完成帧 */
        int id = !index;/* 正在接收的帧 */

        drv->cim_frm_done++;

        if (drv->trans_list[0] != drv->trans_list[1]) {
            usable_frm = drv->trans_list[index];
        }

        /*
         * 1. 优先从空闲列表中获取新的帧, 加入到接收列表中
         * 2. 如果空闲列表没有帧, 则从传输完成列表中获取
         * 3. 如果传输完成列表也没有帧,就用当前传输帧作为保底
         */
        frm = get_free_frm();
        if (!frm)
            frm =  get_usable_frm();

        if (!frm)
            frm =  drv->trans_list[id];

        set_transfer_frame(index, frm);

        if (usable_frm) {
            usable_frm->info.sequence = drv->cim_frm_done;
            usable_frm->info.timestamp = get_time_us();
            usable_frm->info.shutter_count = shutter_count + 1;
            add_to_usable_list(usable_frm);
            critical_thread_cond_signal(&drv->cond);
        }

        /* re-start next dma frame. */
        if (get_bit_field(&status, INT_FLAG_EOW)) {
            cim_set_dma_desc_addr(id);
            cim_start();
        }
        goto out;
    }

    if (get_bit_field(&status, INT_FLAG_SZ_ERR)) {
        cim_set_bit(CIM_CLR_ST, CLR_ST_SIZE_ERR, 1);

        unsigned int active_val = cim_read_reg(CIM_ACT_SIZE);
        unsigned int width = active_val & 0x7ff;
        unsigned int height = (active_val >> 16) & 0x7ff;
        printf("cim size err width=%d height=%d\n", width, height);
        goto out;
    }

    if (get_bit_field(&status, INT_FLAG_SOF)) {
        cim_set_bit(CIM_CLR_ST, CLR_ST_SOF, 1);
        if(drv->camera.sensor->ops.frame_start_callback){
            if(drv->camera.sensor->ops.frame_start_callback() >= 1) {
                shutter_count = drv->camera.sensor->ops.frame_start_callback();
            }
        } else {
            shutter_count = 0;
        }
        goto out;
    }

    if (get_bit_field(&status, INT_FLAG_OVER)) {
        cim_set_bit(CIM_CLR_ST, CLR_ST_OVER, 1);
        printf("cim overflow\n");

        cim_dump_debug_desc();
        cim_dump_regs();
        goto out;
    }

    if (get_bit_field(&status, INT_FLAG_GSA)) {
        cim_set_bit(CIM_CLR_ST, CLR_ST_GSA, 1);
        printf("cim general stop\n");
        goto out;
    }

out:
    return ;
}


static int init_frame_desc(struct frame_desc *desc, int index, void *addr,
        struct frame_desc *next, struct sensor_attr *sensor)
{
    int pixel_fmt = sensor->info.data_fmt;

    switch (pixel_fmt) {
    case CAMERA_PIX_FMT_GREY:
    case CAMERA_PIX_FMT_SBGGR8:
    case CAMERA_PIX_FMT_SGBRG8:
    case CAMERA_PIX_FMT_SGRBG8:
    case CAMERA_PIX_FMT_SRGGB8:
    case CAMERA_PIX_FMT_SBGGR16:
    case CAMERA_PIX_FMT_SGBRG16:
    case CAMERA_PIX_FMT_SGRBG16:
    case CAMERA_PIX_FMT_SRGGB16:
        desc->desc_cfg_t.data.write_back_format = FRM_WRBK_FMT_MONO;
        break;

    case CAMERA_PIX_FMT_UYVY:
    case CAMERA_PIX_FMT_VYUY:
    case CAMERA_PIX_FMT_YUYV:
    case CAMERA_PIX_FMT_YVYU:
        desc->desc_cfg_t.data.write_back_format = FRM_WRBK_FMT_YUV422;
        break;

    case CAMERA_PIX_FMT_RGB565:
        desc->desc_cfg_t.data.write_back_format = FRM_WRBK_FMT_RGB565;
        break;

    case CAMERA_PIX_FMT_RBG24:
        desc->desc_cfg_t.data.write_back_format = FRM_WRBK_FMT_RGB888;
        break;

    default:
        printf("cim NOT support format %x\n", pixel_fmt);
        return -EINVAL;
    }

    desc->desc_cfg_t.data.id = index;
    desc->desc_cfg_t.data.desc_end = 0; /* =0:countinue, not the last one */
    desc->desc_intc_t.data.sof_mask = 1; /* =1:start of frame generate interrupt */
    desc->desc_intc_t.data.eof_mask = 1; /* =1:end of frame generate interrupt */

    desc->next_desc_addr = virt_to_phys(next);
    desc->write_back_address = virt_to_phys(addr);

    /* hist disable */
    desc->desc_hist_cfg_t.d32 = 0;
    desc->hist_write_back_addr = 0;

#if 0
    /* (调试ITU模式时验证) ITU656 interlace mode */
    desc->write_back_stride = sensor->info.line_length * 2;
    desc->sf_write_back_addr = desc->write_back_address + desc->write_back_stride;
#else
    /* progressive mode */
    desc->write_back_stride = sensor->info.line_length;
    desc->sf_write_back_addr = 0;
#endif

    return 0;
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

static int cim_alloc_mem(struct sensor_attr *sensor)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    unsigned int line_length; /* 一行存放的数据长度, 即:stride长度 */
    unsigned int pixel_bytes; /* 每个像素由多少字节组成 */
    sensor_pixel_fmt sensor_fmt = sensor->sensor_info.fmt;

    switch (sensor_fmt) {
    case SENSOR_PIXEL_FMT_Y8_1X8:
    case SENSOR_PIXEL_FMT_SBGGR8_1X8:
    case SENSOR_PIXEL_FMT_SGBRG8_1X8:
    case SENSOR_PIXEL_FMT_SGRBG8_1X8:
    case SENSOR_PIXEL_FMT_SRGGB8_1X8:
        line_length = sensor->sensor_info.width;
        pixel_bytes = 1;
        break;

    case SENSOR_PIXEL_FMT_SBGGR10_1X10:
    case SENSOR_PIXEL_FMT_SGBRG10_1X10:
    case SENSOR_PIXEL_FMT_SGRBG10_1X10:
    case SENSOR_PIXEL_FMT_SRGGB10_1X10:
        if (sensor->sensor_info.width % 4) {
            printf("sensor format(%x) is raw10 but width(%d) is not align: 4\n",
                    sensor->info.data_fmt, sensor->sensor_info.width);
            return -EINVAL;
        }
        line_length = sensor->sensor_info.width * 5 / 4;
        pixel_bytes = 1;
        break;

    case SENSOR_PIXEL_FMT_SBGGR12_1X12:
    case SENSOR_PIXEL_FMT_SGBRG12_1X12:
    case SENSOR_PIXEL_FMT_SGRBG12_1X12:
    case SENSOR_PIXEL_FMT_SRGGB12_1X12:
        if (sensor->sensor_info.width % 2) {
            printf("sensor format(%x) is raw12 but width(%d) is not align: 2\n",
                    sensor->info.data_fmt, sensor->sensor_info.width);
            return -EINVAL;
        }

        line_length = sensor->sensor_info.width * 3 / 2;
        pixel_bytes = 1;
        break;

    case SENSOR_PIXEL_FMT_UYVY8_2X8:
    case SENSOR_PIXEL_FMT_VYUY8_2X8:
    case SENSOR_PIXEL_FMT_YUYV8_2X8:
    case SENSOR_PIXEL_FMT_YVYU8_2X8:
    case SENSOR_PIXEL_FMT_RGB565_2X8_BE:
    case SENSOR_PIXEL_FMT_RGB565_2X8_LE:
        line_length = sensor->sensor_info.width;
        pixel_bytes = 2;
        break;

    case SENSOR_PIXEL_FMT_RGB888_1X24:
        line_length = sensor->sensor_info.width;
        pixel_bytes = 3;
        break;

    default:
        printf("cim alloc mem NOT support format %x\n", sensor->info.data_fmt);
        return -EINVAL;
    }

    /* raw10/raw12 需扩展存储空间 */
    unsigned int line_pixel_bytes = ALIGN(line_length, sensor->sensor_info.width) * pixel_bytes;
    unsigned int real_frame_size = line_pixel_bytes * sensor->sensor_info.height;
    unsigned int frame_align_size = ALIGN(real_frame_size, 4096);
    unsigned int mem_cnt = drv->cam_mem_cnt + 1;

    assert(mem_cnt >= 2);

    void *mem = m_dma_alloc_coherent(mem_cnt * frame_align_size);
    if (mem == NULL) {
        printf("camera: failed to alloc mem: %u\n", frame_align_size * mem_cnt);
        return -ENOMEM;
    }

    drv->mem = mem;
    drv->mem_cnt = mem_cnt;

    drv->frames = malloc(mem_cnt * sizeof(drv->frames[0]));
    assert(drv->frames);

    drv->trans_list = malloc(CIM_DMA_DESC_COUNT * sizeof(drv->trans_list[0]));
    assert(drv->trans_list);

    drv->frame_size = frame_align_size;
    sensor->info.fps = (sensor->sensor_info.fps >> 16) / (sensor->sensor_info.fps & 0xFFFF);
    sensor->info.frame_size = real_frame_size;
    sensor->info.frame_align_size = frame_align_size;
    sensor->info.line_length = line_length;

    init_frm_lists();

    /* DMA描述符 */
    drv->frame_descs = m_dma_alloc_coherent(CIM_DMA_DESC_COUNT * sizeof(drv->frame_descs[0]));
    assert(drv->frame_descs);
    init_frame_desc(&drv->frame_descs[0], 0, drv->frames[0].addr, &drv->frame_descs[1], sensor);
    init_frame_desc(&drv->frame_descs[1], 1, drv->frames[1].addr, &drv->frame_descs[0], sensor);

    flush_cache_all();
    //cim_dump_desc();

    return 0;
}

static void cim_free_mem(void)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    free(drv->frames);
    free(drv->trans_list);

    m_dma_free_coherent(drv->mem, drv->frame_size * drv->mem_cnt);
    m_dma_free_coherent(drv->frame_descs, CIM_DMA_DESC_COUNT * sizeof(drv->frame_descs[0]));

    drv->mem = NULL;
    drv->frames = NULL;
    drv->trans_list = NULL;
    drv->frame_descs = NULL;
}


static void cim_init_common_setting(struct sensor_attr *attr)
{
    unsigned long glb_cfg = cim_read_reg(CIM_GLB_CFG);
    unsigned int resolution_height = 0;
    unsigned int resolution_width = 0;

    /* 接口类型 */
    switch (attr->dbus_type) {
    case SENSOR_DATA_BUS_MIPI:
        set_bit_field(&glb_cfg, GLB_CFG_DAT_IF_SEL, 1);
        break;

    case SENSOR_DATA_BUS_DVP:
        set_bit_field(&glb_cfg, GLB_CFG_DAT_IF_SEL, 0);
        break;

    default:
        printf("cim unknown dbus_type: %d\n", attr->dbus_type);
        assert(0);
        return;
    }

    /* Frame format */
    switch (attr->info.data_fmt) {
    case CAMERA_PIX_FMT_GREY:
    case CAMERA_PIX_FMT_SBGGR8:
    case CAMERA_PIX_FMT_SGBRG8:
    case CAMERA_PIX_FMT_SGRBG8:
    case CAMERA_PIX_FMT_SRGGB8:
    case CAMERA_PIX_FMT_SBGGR16:
    case CAMERA_PIX_FMT_SGBRG16:
    case CAMERA_PIX_FMT_SGRBG16:
    case CAMERA_PIX_FMT_SRGGB16:
        resolution_height = attr->info.height;
        resolution_width = attr->info.line_length;

        set_bit_field(&glb_cfg, GLB_CFG_FRM_FORMAT, CIM_FRAME_FMT_MONO);
        break;

    case CAMERA_PIX_FMT_YUYV:
        resolution_height = attr->info.height;
        resolution_width = attr->info.width;

        set_bit_field(&glb_cfg, GLB_CFG_FRM_FORMAT, CIM_FRAME_FMT_YUV422);
        set_bit_field(&glb_cfg, GLB_CFG_COLOR_ORDER, CIM_FRAME_ORDER_YUYV);
        break;

    case CAMERA_PIX_FMT_YVYU:
        resolution_height = attr->info.height;
        resolution_width = attr->info.width;
        set_bit_field(&glb_cfg, GLB_CFG_FRM_FORMAT, CIM_FRAME_FMT_YUV422);
        set_bit_field(&glb_cfg, GLB_CFG_COLOR_ORDER, CIM_FRAME_ORDER_YVYU);
        break;

    case CAMERA_PIX_FMT_UYVY:
        resolution_height = attr->info.height;
        resolution_width = attr->info.width;
        set_bit_field(&glb_cfg, GLB_CFG_FRM_FORMAT, CIM_FRAME_FMT_YUV422);
        set_bit_field(&glb_cfg, GLB_CFG_COLOR_ORDER, CIM_FRAME_ORDER_UYVY);
        break;

    case CAMERA_PIX_FMT_VYUY:
        resolution_height = attr->info.height;
        resolution_width = attr->info.width;
        set_bit_field(&glb_cfg, GLB_CFG_FRM_FORMAT, CIM_FRAME_FMT_YUV422);
        set_bit_field(&glb_cfg, GLB_CFG_COLOR_ORDER, CIM_FRAME_ORDER_VYUY);
        break;

    case CAMERA_PIX_FMT_RGB565:
        resolution_height = attr->info.height;
        resolution_width = attr->info.width;
        set_bit_field(&glb_cfg, GLB_CFG_FRM_FORMAT, CIM_FRAME_FMT_RGB565);
        set_bit_field(&glb_cfg, GLB_CFG_COLOR_ORDER, CIM_FRAME_ORDER_RGB);
        break;

    case CAMERA_PIX_FMT_RBG24:
        resolution_height = attr->info.height;
        resolution_width = attr->info.width;
        set_bit_field(&glb_cfg, GLB_CFG_FRM_FORMAT, CIM_FRAME_FMT_RGB888);
        set_bit_field(&glb_cfg, GLB_CFG_COLOR_ORDER, CIM_FRAME_ORDER_RGB);
        break;

    default:
        printf("cim init common setting NOT support format %x\n", attr->info.data_fmt);
        assert(0);
        return;
    }

    /* set dma burst length */
    set_bit_field(&glb_cfg, GLB_CFG_BURST_LEN, CIM_DMA_BURST_LEN_32);

    /* enable auto recovery */
    set_bit_field(&glb_cfg, GLB_CFG_AUTO_RECOVERY, 1);

    /* frame size check enable/disable */
    if (is_frame_size_check)
        set_bit_field(&glb_cfg, GLB_CFG_SIZE_CHK, 1);
    else
        set_bit_field(&glb_cfg, GLB_CFG_SIZE_CHK, 0);

    /* disable snapshot */
    if (is_enable_snapshot) {
        unsigned char expo_width = 0x08;
        set_bit_field(&glb_cfg, GLB_CFG_SNAPSHOT_MODE, 1);
        set_bit_field(&glb_cfg, GLB_CFG_EXPO_WIDTH, expo_width - 1);

        unsigned int delay_num = 0x08; /* 该值目前随机 */
        unsigned long delay_cfg;
        set_bit_field(&delay_cfg, DLY_CFG_EN, 1);
        set_bit_field(&delay_cfg, DLY_CFG_MD, 1);
        set_bit_field(&delay_cfg, DLY_CFG_NUM, delay_num);

        cim_write_reg(CIM_DLY_CFG, delay_cfg);
    } else {
        cim_write_reg(CIM_DLY_CFG, 0);
    }

    /* image no resize */
    unsigned long resolution = 0;
#if 0
    /* (调试ITU模式时验证) ITU656 interlace mode */
    set_bit_field(&resolution, CROP_SIZE_HEIGHT, resolution_height / 2);
#else
    /* progressive mode */
    set_bit_field(&resolution, CROP_SIZE_HEIGHT, resolution_height);
#endif

    set_bit_field(&resolution, CROP_SIZE_WIDTH,  resolution_width);

    cim_write_reg(CIM_CROP_SIZE, resolution);
    cim_write_reg(CIM_CROP_SITE, 0);

    /* interrupt setting */
    unsigned long interrupt = 0;
    /* 停止正常工作帧中断 */
    set_bit_field(&interrupt, INTC_MSK_EOW,  1);

    /*
     * 一帧采集开始/结束中断
     * Work together with DES_INTC.EOF_MSK
     * only when both are 1 will generate interrupt
     */
    set_bit_field(&interrupt, INTC_MSK_EOF,  1);

    set_bit_field(&interrupt, INTC_MSK_SOF,  1);


    /* enable general stop of frame interrupt */
    set_bit_field(&interrupt, INTC_MSK_GSA,  1);

    /* enable rxfifo overflow interrupt */
    set_bit_field(&interrupt, INTC_MSK_OVER,  1);

    /* enable size check err */
    if (is_frame_size_check)
        set_bit_field(&interrupt, INTC_MSK_SZ_ERR,  1);
    else
        set_bit_field(&interrupt, INTC_MSK_SZ_ERR,  0);

    cim_write_reg(CIM_DBG_CGC, 0x00);
    cim_write_reg(CIM_GLB_CFG, glb_cfg);
    cim_write_reg(CIM_INTC, interrupt);
    cim_write_reg(CIM_QOS_CTRL, 0);
}

static void cim_init_dvp_timming(struct sensor_attr *attr)
{
    unsigned long glb_cfg = cim_read_reg(CIM_GLB_CFG);

    /* PCLK Polarity */
    if (attr->dvp.pclk_polarity == POLARITY_SAMPLE_RISING)
        set_bit_field(&glb_cfg, GLB_CFG_EDGE_PCLK, CIM_PCLK_SAMPLE_RISING);
    else
        set_bit_field(&glb_cfg, GLB_CFG_EDGE_PCLK, CIM_PCLK_SAMPLE_FALLING);

    /* VSYNC Polarity */
    if (attr->dvp.vsync_polarity == POLARITY_HIGH_ACTIVE)
        set_bit_field(&glb_cfg, GLB_CFG_LEVEL_VSYNC, CIM_VSYNC_ACTIVE_HIGH);
    else
        set_bit_field(&glb_cfg, GLB_CFG_LEVEL_VSYNC, CIM_VSYNC_ACTIVE_LOW);

    /* HSYNC Polarity */
    if (attr->dvp.hsync_polarity == POLARITY_HIGH_ACTIVE)
        set_bit_field(&glb_cfg, GLB_CFG_LEVEL_HSYNC, CIM_HSYNC_ACTIVE_HIGH);
    else
        set_bit_field(&glb_cfg, GLB_CFG_LEVEL_HSYNC, CIM_HSYNC_ACTIVE_LOW);

    cim_write_reg(CIM_GLB_CFG, glb_cfg);
}


static void cim_dvp_init_setting(struct sensor_attr *attr)
{
    assert_range(attr->sensor_info.width, 1, 2047);
    assert_range(attr->sensor_info.height, 1, 2047);
    assert(attr->dbus_type == SENSOR_DATA_BUS_DVP);

    cim_init_common_setting(attr);

    cim_init_dvp_timming(attr);

    cim_init_dma_desc();

    cim_set_dma_desc_addr(0);

    cim_clear_status_all();
}

static void cim_init_mipi_timming(struct sensor_attr *attr)
{
    unsigned long glb_cfg = cim_read_reg(CIM_GLB_CFG);

    /* PCLK Polarity */
    set_bit_field(&glb_cfg, GLB_CFG_EDGE_PCLK, CIM_PCLK_SAMPLE_FALLING);

    /* VSYNC Polarity */
    set_bit_field(&glb_cfg, GLB_CFG_LEVEL_VSYNC, CIM_VSYNC_ACTIVE_HIGH);

    /* HSYNC Polarity */
    set_bit_field(&glb_cfg, GLB_CFG_LEVEL_HSYNC, CIM_HSYNC_ACTIVE_HIGH);

    cim_write_reg(CIM_GLB_CFG, glb_cfg);
}

static void cim_mipi_init_setting(struct sensor_attr *attr)
{
    assert_range(attr->sensor_info.width, 1, 2047);
    assert_range(attr->sensor_info.height, 1, 2047);
    assert_range(attr->mipi.lanes, 1, 2);
    assert(attr->dbus_type == SENSOR_DATA_BUS_MIPI);

    cim_init_common_setting(attr);

    cim_init_mipi_timming(attr);

    cim_init_dma_desc();

    cim_set_dma_desc_addr(0);

    /* CIM 只支持 csi0输入 */
    int csi_ret = mipi_csi_phy_initialization(0, &attr->mipi);
    assert(csi_ret >= 0);

    cim_clear_status_all();
}

static void cim_init_setting(struct sensor_attr *attr)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    clk_enable(drv->cim_gate_clk);

    reset_frm_lists();

    /* ITU656 ITU1120暂未支持 */
    if (attr->dbus_type == SENSOR_DATA_BUS_DVP)
        cim_dvp_init_setting(attr);
    else if (attr->dbus_type == SENSOR_DATA_BUS_MIPI)
        cim_mipi_init_setting(attr);
    else
        panic("camera: not support this type now: %d\n", attr->dbus_type);
}

static void cim_deinit_setting(struct sensor_attr *attr)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    /* CIM 只支持 csi0输入 */
    if (attr->dbus_type == SENSOR_DATA_BUS_MIPI)
        mipi_csi_phy_stop(0);

    clk_disable(drv->cim_gate_clk);
}

static void cim_stream_on(struct sensor_attr *attr)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    cim_soft_reset();

    cim_start();

    //cim_dump_regs();

    enable_irq(drv->irq);
}

static void cim_stream_off(struct sensor_attr *attr)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    disable_irq(drv->irq);

    cim_set_bit(CIM_CTRL, CTRL_QUICK_STOP, 1);
    cim_write_reg(CIM_CLR_ST, 0xFF);

    cim_soft_reset();

    /* CIM 只支持 csi0输入 */
    if (attr->dbus_type == SENSOR_DATA_BUS_MIPI)
        mipi_csi_phy_stop(0);

    clk_disable(drv->cim_gate_clk);
}


/*
 * 从内存中的最后有效数据向前排列
 * 小端排序, 低6位补0
 * mipi raw10 格式排序转换为raw16
 * raw10在内存中的排列顺序
 * MSB                                               LSB
 *   8bit         8bit       8bit         8bit
 * [d9 ~ d2]   [c9 ~ c2]   [b9 ~ b2]   [a9 ~ a2]
 * [ ....  ]   [ ....  ]   [ ....  ]   [d1d0c1c0b1b0a1a0]
 *
 *
 *
 * 转换后raw16在内存中的排列顺序
 *   8bit         8bit         8bit         8bit
 * [b9 ~ b2]   [b1b0------] [a9 ~ a2]   [a1a0------]
 * [d9 ~ d2]   [d1d0------] [c9 ~ c2]   [c1c0------]
 *
 */
static void frame_convert_raw10_padding_raw16_forward(unsigned char *src, unsigned short *dst, int length)
{
    while (length) {
        src -= 5;
        unsigned short a = (src[0] << 8) | ((src[4] << 6) & 0xC0);
        unsigned short b = (src[1] << 8) | ((src[4] << 4) & 0xC0);
        unsigned short c = (src[2] << 8) | ((src[4] << 2) & 0xC0);
        unsigned short d = (src[3] << 8) | ((src[4] << 0) & 0xC0);

        dst -= 4;
        dst[0] = a;
        dst[1] = b;
        dst[2] = c;
        dst[3] = d;

        length -= 5;
    }
}

/*
 * 从内存中的最后有效数据向前排列
 * 小端排序, 低4位补0
 *
 * mipi raw12 格式排序转换为raw16
 * raw12在内存中的排列顺序
 * MSB                                                    LSB
 *   8bit                8bit            8bit         8bit
 * [ ....  ]     [b3b2b1b0a3a2a1a0]   [b11 ~ b4]   [a11 ~ a4]
 *
 *
 *
 * 转换后raw16在内存中的排列顺序
 *   8bit             8bit               8bit         8bit
 * [b11 ~ b4]    [b3b2b1b0----]       [a11 ~ a4]   [a3a2a1a0----]
 *
 */
static void frame_convert_raw12_padding_raw16_forward(unsigned char *src, unsigned char *dst, int length)
{
    while (length) {
        src -= 3;
        unsigned short a = (src[0] << 8) | ((src[2] << 4) & 0xF0);
        unsigned short b = (src[1] << 8) | ((src[2] << 0) & 0xF0);

        dst -= 2;
        dst[0] = a;
        dst[1] = b;

        length -= 3;
    }
}

static void frame_format_convert(struct jz_cim_data *drv, void *address)
{
    struct sensor_attr *attr = drv->camera.sensor;
    sensor_pixel_fmt sensor_fmt = attr->sensor_info.fmt;

    /* CSI实际接收到的有效数据大小 */
    unsigned int real_frame_size = attr->info.line_length * attr->info.height;

    switch (sensor_fmt) {
    case SENSOR_PIXEL_FMT_SBGGR10_1X10:
    case SENSOR_PIXEL_FMT_SGBRG10_1X10:
    case SENSOR_PIXEL_FMT_SGRBG10_1X10:
    case SENSOR_PIXEL_FMT_SRGGB10_1X10:
        /* 从后向前将数据扩展为raw16格式 */
        frame_convert_raw10_padding_raw16_forward(
                address + real_frame_size,
                address + attr->info.frame_size,
                real_frame_size);
        break;

    case SENSOR_PIXEL_FMT_SBGGR12_1X12:
    case SENSOR_PIXEL_FMT_SGBRG12_1X12:
    case SENSOR_PIXEL_FMT_SGRBG12_1X12:
    case SENSOR_PIXEL_FMT_SRGGB12_1X12:
        /* 从后向前将数据扩展为raw16格式 */
        frame_convert_raw12_padding_raw16_forward(
                address + real_frame_size,
                address + attr->info.frame_size,
                real_frame_size);
        break;

    default:
        break;
    }
}

camera_frame_error_type soc_cim_get_frame_error(int index)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    struct camera_device *camera = &drv->camera;

    if (drv->wait_timeout)
        return camera_error_timeout;

    if (!camera->is_stream_on)
        return camera_error_stream_is_off;

    return camera_error_null;
}

static inline struct frame_data *cim_mem_2_frame_data(void *mem)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    unsigned int size = mem - drv->mem;
    unsigned int count = size / drv->frame_size;
    struct frame_data *frm = &drv->frames[count];

    assert(!(size % drv->frame_size));
    assert(count < drv->mem_cnt);

    return frm;
}

static int check_frame_info(struct frame_info *info)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    struct frame_data *frm = &drv->frames[info->index];

    if (info->index >= drv->mem_cnt)
        return -1;

    if (frm->info.paddr != info->paddr)
        return -1;

    return 0;
}

static inline struct frame_data *cim_frame_info_2_frame_data(struct frame_info *info)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    if (!check_frame_info(info))
        return &drv->frames[info->index];
    else {
        return NULL;
    }
}

static int cim_check_frame_mem(void *mem)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    unsigned int size = mem - drv->mem;

    if (mem < drv->mem)
        return -1;

    if (size % drv->frame_size)
        return -1;

    if (size / drv->frame_size >= drv->mem_cnt)
        return -1;

    return 0;
}

static int cim_get_frame(int index, struct frame_data **frame, unsigned int timeout_ms)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    struct camera_device *camera = &drv->camera;
    struct frame_data *frm = NULL;
    camera_frame_error_type err;
    int ret = -EAGAIN;

    assert(camera);

    os_enter_critical();

    err = soc_cim_get_frame_error(index);
    if (err != camera_error_null)
        goto unlock;

    frm = get_usable_frm();

    if (!frm && timeout_ms) {
        int ret = critical_thread_cond_wait_timeout(&drv->cond, timeout_ms);
        if (ret) {
            if (camera->is_stream_on)
                drv->wait_timeout = 1;
            goto unlock;
        }

        frm = get_usable_frm();
    }

    err = soc_cim_get_frame_error(index);
    if (err != camera_error_null) {
        add_to_free_list(frm);
        goto unlock;
    }

    if(frm){
        frm->status = frame_status_user;
        *frame = frm;
        ret = 0;
    }

unlock:
    os_exit_critical();

    if(frm){
        m_cache_sync(frm->addr, drv->frame_size);
        frame_format_convert(drv, frm->addr);
    }

    return ret;
}

static void cim_put_frame(int index, struct frame_data *frm)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    m_cache_sync(frm->addr, frm->info.size);

    os_enter_critical();

    if (frm->status != frame_status_user) {
        printf("[%s] frame_index %u, %d\n", __func__, frm->info.index, frm->status);
        goto unlock;
    }

    list_del_init(&frm->link);

    if (drv->trans_list[0] == drv->trans_list[1]) {
        int index = cim_read_reg(CIM_FRM_ID);
        set_transfer_frame(index, frm);

    } else {
        add_to_free_list(frm);
    }

unlock:
    os_exit_critical();

}

/******************************************************************************
 *
 * 应用调用 函数实现
 *
 *****************************************************************************/
int soc_cim_detect(int index)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    int ret = -ENODEV;

    mutex_lock(&drv->lock);

    if (!drv->camera.sensor) {
        printf("cim: no sensor regisered\n");
        goto unlock;
    }

    if (drv->camera.is_power_on) {
        ret = 0;
        goto unlock;
    }

    if (!drv->camera.sensor->ops.power_on()) {
        drv->camera.is_power_on = 1;

        drv->cim_frm_done = 0;
        drv->cim_frm_output = 0;
        ret = 0;
    }

unlock:
    mutex_unlock(&drv->lock);

    return ret;
}

struct camera_info *soc_cim_get_info(int index)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    struct camera_device *camera = &drv->camera;

    return &camera->sensor->info;
}

int soc_cim_power_on(int index)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    struct camera_device *camera = &drv->camera;
    int ret = 0;

    mutex_lock(&drv->lock);

    wake_lock(&drv->wake_lock);

    if (!camera->is_power_on) {
        ret = camera->sensor->ops.power_on();
        if (!ret) {
            drv->camera.is_power_on = 1;

            drv->cim_frm_done = 0;
            drv->cim_frm_output = 0;
        }
        else
            wake_unlock(&drv->wake_lock);
    }

    mutex_unlock(&drv->lock);

    return ret;
}


void soc_cim_power_off(int index)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    struct camera_device *camera = &drv->camera;
    struct sensor_attr *attr = drv->camera.sensor;

    mutex_lock(&drv->lock);

    if (camera->is_stream_on) {
        cim_stream_off(attr);
        camera->sensor->ops.stream_off();
        camera->is_stream_on = 0;
    }

    if (camera->is_power_on) {
        camera->sensor->ops.power_off();
        camera->is_power_on = 0;
    }

    wake_unlock(&drv->wake_lock);

    mutex_unlock(&drv->lock);
}

int soc_cim_stream_on(int index)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    struct camera_device *camera = &drv->camera;
    struct sensor_attr *attr = drv->camera.sensor;
    int ret = 0;

    mutex_lock(&drv->lock);

    if (!camera->is_power_on) {
        printf("cim : camera can't stream on when not power on\n");
        ret = -EINVAL;
        goto unlock;
    }

    if (camera->is_stream_on)
        goto unlock;

    cim_init_setting(attr);

    ret = attr->ops.stream_on();
    if (ret) {
        cim_deinit_setting(attr);
        goto unlock;
    }

    cim_stream_on(attr);

    camera->is_stream_on = 1;

unlock:
    mutex_unlock(&drv->lock);

    return ret;
}

void soc_cim_stream_off(int index)
{
    struct jz_cim_data *drv = &jz_cim_dev;
    struct camera_device *camera = &drv->camera;
    struct sensor_attr *attr = drv->camera.sensor;

    mutex_lock(&drv->lock);

    if (camera->is_stream_on) {
        cim_stream_off(attr);
        attr->ops.stream_off();
        drv->camera.is_stream_on = 0;
    }

    mutex_unlock(&drv->lock);
}

void *soc_cim_get_frame(int index)
{
    int ret;
    struct frame_data *frm;

    ret =cim_get_frame(index, &frm, 0);
    if (ret)
        return NULL;

    return frm->info.vaddr;
}

void *soc_cim_wait_frame(int index)
{
    int ret;
    struct frame_data *frm;

    ret = cim_get_frame(index, &frm, 3000);
    if (ret)
        return NULL;

    return frm->info.vaddr;
}

int soc_cim_put_frame(int index, void *buf)
{
    struct frame_data *frm;
    if (cim_check_frame_mem(buf))
        return -EINVAL;

    frm = cim_mem_2_frame_data(buf);
    cim_put_frame(index, frm);

    return 0;
}

int soc_cim_dqbuf(int index, struct frame_info *frame)
{
    int ret;
    struct frame_data *frm;

    assert(frame);

    ret = cim_get_frame(index, &frm, 0);
    if (ret)
        return ret;

    memcpy(frame, &frm->info, sizeof(struct frame_info));

    return 0;
}

int soc_cim_dqbuf_wait(int index, struct frame_info *frame)
{
    int ret;
    struct frame_data *frm;

    assert(frame);

    ret = cim_get_frame(index, &frm, 3000);
    if (ret)
        return ret;

    memcpy(frame, &frm->info, sizeof(struct frame_info));

    return 0;
}

int soc_cim_qbuf(int index, struct frame_info *frame)
{
    struct frame_data *frm;

    assert(frame);

    frm = cim_frame_info_2_frame_data(frame);
    if (!frm) {
        printf("cim_frame_info_2_frame_data fail %p\n", frm);
        return -EINVAL;
    }

    cim_put_frame(index, frm);

    return 0;
}

int soc_cim_set_hal_sensor_reg(int index, struct sensor_dbg_register *reg)
{
    struct jz_cim_data *drv = &jz_cim_dev;
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

int soc_cim_get_hal_sensor_reg(int index, struct sensor_dbg_register *reg)
{
    struct jz_cim_data *drv = &jz_cim_dev;
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

unsigned int soc_cim_get_available_frame_count(int index)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    return drv->frame_counter;
}

void soc_cim_skip_frames(int index, unsigned int frames)
{
    os_enter_critical();

    while (frames--) {
        struct frame_data *frm = get_usable_frm();
        if (frm == NULL)
            break;
        add_to_free_list(frm);
    }

    os_exit_critical();
}

#define error_if(_cond)                                     \
    do {                                                    \
        if (_cond) {                                        \
            printf("cim: failed to check: %s\n", #_cond);   \
            ret = -1;                                       \
            goto unlock;                                    \
        }                                                   \
    } while (0)


/*
 * 格式信息转换 转换后的格式经过CIM DMA 到DDR
 * sensor format : Sensor格式在sensor driver中根据setting指定
 * camera format : Camera格式在camera driver中使用,并暴露给应用
 *
 *    sensor格式                  Camera格式
 * [成员0 sensor format]   <--->  [成员1 camera format]
 *
 */
struct format_mapping {
    sensor_pixel_fmt sensor_fmt;
    camera_pixel_fmt camera_fmt;
};

static struct format_mapping sensor_camera_format_map[] = {
    {SENSOR_PIXEL_FMT_Y8_1X8,           CAMERA_PIX_FMT_GREY},

    {SENSOR_PIXEL_FMT_SBGGR8_1X8,       CAMERA_PIX_FMT_SBGGR8},
    {SENSOR_PIXEL_FMT_SGBRG8_1X8,       CAMERA_PIX_FMT_SGBRG8},
    {SENSOR_PIXEL_FMT_SGRBG8_1X8,       CAMERA_PIX_FMT_SGRBG8},
    {SENSOR_PIXEL_FMT_SRGGB8_1X8,       CAMERA_PIX_FMT_SRGGB8},

    {SENSOR_PIXEL_FMT_SBGGR10_1X10,     CAMERA_PIX_FMT_SBGGR16},
    {SENSOR_PIXEL_FMT_SGBRG10_1X10,     CAMERA_PIX_FMT_SGBRG16},
    {SENSOR_PIXEL_FMT_SGRBG10_1X10,     CAMERA_PIX_FMT_SGRBG16},
    {SENSOR_PIXEL_FMT_SRGGB10_1X10,     CAMERA_PIX_FMT_SRGGB16},

    {SENSOR_PIXEL_FMT_SBGGR12_1X12,     CAMERA_PIX_FMT_SBGGR16},
    {SENSOR_PIXEL_FMT_SGBRG12_1X12,     CAMERA_PIX_FMT_SGBRG16},
    {SENSOR_PIXEL_FMT_SGRBG12_1X12,     CAMERA_PIX_FMT_SGRBG16},
    {SENSOR_PIXEL_FMT_SRGGB12_1X12,     CAMERA_PIX_FMT_SRGGB16},

    {SENSOR_PIXEL_FMT_UYVY8_2X8,        CAMERA_PIX_FMT_UYVY},
    {SENSOR_PIXEL_FMT_VYUY8_2X8,        CAMERA_PIX_FMT_VYUY},
    {SENSOR_PIXEL_FMT_YUYV8_2X8,        CAMERA_PIX_FMT_YUYV},
    {SENSOR_PIXEL_FMT_YVYU8_2X8,        CAMERA_PIX_FMT_YVYU},

    {SENSOR_PIXEL_FMT_RGB565_2X8_BE,    CAMERA_PIX_FMT_RGB565},
    {SENSOR_PIXEL_FMT_RGB565_2X8_LE,    CAMERA_PIX_FMT_RGB565},

    {SENSOR_PIXEL_FMT_RGB888_1X24,      CAMERA_PIX_FMT_RBG24},
};

static int sensor_attribute_check_init(struct sensor_attr *sensor)
{
    int ret = -EINVAL;

    error_if(!sensor->device_name);
    error_if(sensor->sensor_info.width < 64 || sensor->sensor_info.width > 2046);
    error_if(sensor->sensor_info.height < 64 || sensor->sensor_info.height > 2046);
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

    sensor->info.data_fmt = sensor_camera_format_map[i].camera_fmt;

    if (sensor->dbus_type == SENSOR_DATA_BUS_DVP) {
        switch(sensor->dvp.gpio_mode) {
        case DVP_PA_LOW_8BIT:
            break;

        default:
            printf("attribute check: DVP Unsupported gpio mode(%d) .\n", sensor->dvp.gpio_mode);
            goto unlock;
        }
    }

    return 0;

unlock:
    return ret;
}

int soc_cim_register_sensor_routine(int index, int mem_cnt, struct sensor_attr *sensor)
{
    assert(index == 2);

    struct jz_cim_data *drv = &jz_cim_dev;
    int ret = 0;

    assert(drv->is_finish > 0);
    assert(!drv->camera.sensor);

    ret = sensor_attribute_check_init(sensor);
    assert(ret == 0);

    mutex_lock(&drv->lock);

    drv->cam_mem_cnt = mem_cnt;
    if (drv->cam_mem_cnt < 1) {
        drv->cam_mem_cnt = 2;
        printf("cim device(camera): mem cnt fix to 2\n");
    }

    drv->camera.sensor = sensor;
    ret = cim_alloc_mem(sensor);
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

void soc_cim_unregister_sensor_routine(int index, struct sensor_attr *sensor)
{
    assert(index == 2);

    struct jz_cim_data *drv = &jz_cim_dev;

    assert(drv->is_finish > 0);
    assert(drv->camera.sensor);
    assert(sensor == drv->camera.sensor);

    mutex_lock(&drv->lock);

    if (drv->camera.is_stream_on)
        panic("cim : failed to unregister, when camera stream on!\n");

    if (drv->camera.is_power_on) {
        struct sensor_attr *attr = drv->camera.sensor;
        attr->ops.power_off();

        drv->camera.is_power_on = 0;
    }
    drv->camera.sensor = NULL;

    cim_free_mem();

    mutex_unlock(&drv->lock);
}


int jz_cim_drv_init(void)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    drv->cim_gate_clk = clk_get(drv->cim_gate_clk_name); /* 目前在此未使用 */
    assert(drv->cim_gate_clk);

    critical_thread_cond_init(&drv->cond);

    mutex_init(&drv->lock);
    spin_lock_init(&drv->spinlock);
    wake_lock_init(&drv->wake_lock, "cim_wake_lock");

    request_irq_disabled(drv->irq, 0, cim_irq_handler, drv->irq_name, drv);

    drv->is_finish = 1;

    return 0;
}

void jz_cim_drv_deinit(void)
{
    struct jz_cim_data *drv = &jz_cim_dev;

    if (!drv->is_finish)
        return ;

    drv->is_finish = 0;

    release_irq(drv->irq);
}


