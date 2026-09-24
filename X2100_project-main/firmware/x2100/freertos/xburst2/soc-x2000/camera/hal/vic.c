/*
 * Copyright (C) 2020 Ingenic Semiconductor Co., Ltd.
 *
 * Camera Interface driver for the Ingenic controller
 *
 */
#include <common.h>
#include <list.h>
#include <os.h>
#include <bit_field.h>
#include <spinlock.h>
#include <driver/irq.h>
#include <driver/clk.h>
#include <driver/gpio.h>
#include <driver/camera.h>

#include "camera_cpm.h"
#include "camera_gpio.h"
#include "csi.h"
#include "vic.h"
#include "../cim/cim.h"
#include "../vic/vic_channel_mem.h"
#include "../isp/vic_channel_tiziano.h"

struct jz_camera_data {
    int index;
    int is_enable;
    int is_finish;
    int mclk_io;                    /* MCLK输出管脚选择: PC15(3.3V) / PE24(1.8V) */

    int is_isp_enable;              /* ISP Enable功能
                                     * =1, VIC ---> ISP
                                     * =0, VIC ---> DDR
                                     */

    const char *isp_power_clk_name; /* 暂时未用 */
    const char *isp_gate_clk_name;
    const char *isp_div_clk_name;   /* ISP0/1 共用 */
    const char *mclk_gate_clk_name; /* VIC/CIM 可共用 */

    struct clk *mclk_div;           /* VIC/CIM 可共用 */
    struct clk *mclk_gate;          /* VIC/CIM 可共用 */
    struct clk *isp_div;
    struct clk *isp_power_clk;      /* 暂时未用 */
    struct clk *isp_gate_clk;
    unsigned long isp_clk_rate;

    /* Camera Device */
    unsigned int cam_mem_cnt;       /* 循环buff个数(针对VIC MEM有效, 经过ISP该参数无效) */
    struct camera_device camera;

    struct mutex lock;
};


static struct jz_camera_data jz_camera_dev[3] = {
    {
        #ifdef CONFIG_SOC_X2000_CAMERA_VIC0
        .index                  = 0,
        .is_enable              = 1,
        .mclk_io                = CONFIG_SOC_X2000_CAMERA_VIC0_MCLK,
        .isp_div_clk_name       = "cgu_isp",
        .isp_power_clk_name     = "power_isp0", /* 当前未实现  CPM_LCR[31] [27]操作代替 */
        .isp_gate_clk_name      = "gate_isp0",
        .mclk_gate_clk_name     = "gate_cim",
        .cam_mem_cnt            = CONFIG_SOC_X2000_CAMERA_VIC0_FRAME_NUMS,
        #ifdef CONFIG_SOC_X2000_CAMERA_VIC0_ROUTINE_ISP
        .is_isp_enable          = 1,
        #else
        .is_isp_enable          = 0,
        #endif
        #endif
    },

    {
        #ifdef CONFIG_SOC_X2000_CAMERA_VIC1
        .index                  = 1,
        .is_enable              = 1,
        .mclk_io                = CONFIG_SOC_X2000_CAMERA_VIC1_MCLK,
        .isp_div_clk_name       = "cgu_isp",
        .isp_power_clk_name     = "power_isp1", /* 当前未实现  CPM_LCR[30] [26]操作代替 */
        .isp_gate_clk_name      = "gate_isp1",
        .mclk_gate_clk_name     = "gate_cim",
        .cam_mem_cnt            = CONFIG_SOC_X2000_CAMERA_VIC1_FRAME_NUMS,
        #ifdef CONFIG_SOC_X2000_CAMERA_VIC1_ROUTINE_ISP
        .is_isp_enable          = 1,
        #else
        .is_isp_enable          = 0,
        #endif
        #endif
    },

    {
        #ifdef CONFIG_SOC_X2000_CAMERA_CIM
        .index                  = 2,
        .is_enable              = 1,
        .mclk_io                = CONFIG_SOC_X2000_CAMERA_CIM_MCLK,
        .mclk_gate_clk_name     = "gate_cim",
        .cam_mem_cnt            = CONFIG_SOC_X2000_CAMERA_CIM_FRAME_NUMS,
        #endif
    },
};


/*
 * 模拟 power_ispX 的实现
 */

static void isp_clk_power_mode_enable(int index)
{
    int ret = 0;
    int timeout = 0xFFFF;

    switch (index) {
    case 0:
        cpm_set_bit(CPM_LCR, CPM_LCR_CTRL_PWDN_ISP0, 0);
        do {
            ret = cpm_get_bit(CPM_LCR, CPM_LCR_STATUS_PWDN_ISP0);
        } while (ret && timeout--);
        break;

    case 1:
        cpm_set_bit(CPM_LCR, CPM_LCR_CTRL_PWDN_ISP1, 0);
        do {
            ret = cpm_get_bit(CPM_LCR, CPM_LCR_STATUS_PWDN_ISP1);
        } while (ret && timeout--);
        break;

    default:
        printf("isp%d is not support\n", index);
        break;
    }

    if (timeout <= 0)
        printf("isp%d power enable failed\n", index);
}

static void isp_clk_power_mode_disable(int index)
{
    int ret = 0;
    int timeout = 0xFFFF;

    switch (index) {
    case 0:
        cpm_set_bit(CPM_LCR, CPM_LCR_CTRL_PWDN_ISP0, 1);
        do {
            ret = cpm_get_bit(CPM_LCR, CPM_LCR_STATUS_PWDN_ISP0);
        } while (!ret && timeout--);
        break;

    case 1:
        cpm_set_bit(CPM_LCR, CPM_LCR_CTRL_PWDN_ISP1, 1);
        do {
            ret = cpm_get_bit(CPM_LCR, CPM_LCR_STATUS_PWDN_ISP1);
        } while (!ret && timeout--);
        break;

    default:
        printf("isp%d is not support\n", index);
        break;
    }

    if (timeout <= 0)
        printf("isp%d power disable failed\n", index);
}

/*
 * 输入为raw8时控制器输出会变成raw16，当我们想得到raw8时，
 * 我们就把raw8数据当yuv数据来处理，将输入输出格式均设为yuv422，
 * 因为输入输出yuv422格式时不会改变原数据，这样我们就可以得到原封不动的raw8数据了。
*/
unsigned int is_output_y8(int index, struct sensor_attr *attr)
{
    if (attr->dbus_type == SENSOR_DATA_BUS_DVP)
        return ( !jz_camera_dev[index].is_isp_enable &&    \
                attr->dvp.data_fmt == DVP_RAW8 &&       \
                (sensor_fmt_is_8BIT(attr->sensor_info.fmt)) );

    if (attr->dbus_type == SENSOR_DATA_BUS_MIPI)
        return ( !jz_camera_dev[index].is_isp_enable &&    \
                attr->mipi.data_fmt == MIPI_RAW8 &&     \
                (sensor_fmt_is_8BIT(attr->sensor_info.fmt)) );

    return 0;
}

/*
 * 输入为yuv422时 DMA控制器可以重新排列输出的顺序,
 * 所以YUV422输入可以选择输出NV12/NV21/Grey格式
*/
unsigned int is_output_yuv422(int index, struct sensor_attr *attr)
{
    if (attr->dbus_type == SENSOR_DATA_BUS_DVP)
        return ( !jz_camera_dev[index].is_isp_enable &&        \
                (sensor_fmt_is_YUV422(attr->sensor_info.fmt)) &&   \
                (attr->dvp.data_fmt == DVP_YUV422 || attr->dvp.data_fmt == DVP_YUV422_8BIT) );

    if (attr->dbus_type == SENSOR_DATA_BUS_MIPI)
        return ( !jz_camera_dev[index].is_isp_enable &&        \
                attr->mipi.data_fmt == MIPI_YUV422 &&       \
                (sensor_fmt_is_YUV422(attr->sensor_info.fmt)) );

    return 0;
}

static void vic_start(int index)
{
    /* reset vic 控制器 */
    vic_set_bit(index, VIC_CONTROL, VIC_START, 1);
}

static void vic_reset(unsigned int index)
{
    /* reset vic 控制器 */
    vic_set_bit(index, VIC_CONTROL, VIC_GLB_RST, 1);
}

static void vic_dma_reset(int index)
{
    /* reset dma */
    vic_write_reg(index, VIC_DMA_RESET, 1);
}

static void vic_register_enable(int index)
{
    /* VIC 开始初始化 */
    vic_set_bit(index ,VIC_CONTROL, VIC_REG_ENABLE, 1);
#if 0
    int timeout = 3000;
    while (vic_get_bit(index ,VIC_CONTROL, VIC_REG_ENABLE)) {
        if (--timeout == 0) {
            printf("timeout while wait vic_reg_enable: %x\n", vic_read_reg(index, VIC_CONTROL));
            break;
        }
    }
#endif
}

static void vic_data_path_select_route(int index, int route)
{
    if (route) {
        /* ISP Route */
        vic_set_bit(index, VIC_CONTROL_DMA_ROUTE, VC_DMA_ROUTE_dma_out, 0);
        vic_set_bit(index, VIC_CONTROL_TIZIANO_ROUTE, VC_TIZIANO_ROUTE_isp_out, 1);
    } else {
        /* DMA Route */
        vic_set_bit(index, VIC_CONTROL_TIZIANO_ROUTE, VC_TIZIANO_ROUTE_isp_out, 0);
        vic_set_bit(index, VIC_CONTROL_DMA_ROUTE, VC_DMA_ROUTE_dma_out, 1);
    }
}


static void vic_init_dvp_timing(int index, struct sensor_attr *attr)
{
    unsigned long vic_input_dvp = vic_read_reg(index, VIC_INPUT_DVP);
    unsigned long yuv_data_order = attr->dvp.yuv_data_order;

    if (is_output_y8(index, attr))
        set_bit_field(&vic_input_dvp, DVP_DATA_FORMAT, 6);
    else if (attr->dvp.data_fmt <= DVP_RAW12)
        set_bit_field(&vic_input_dvp, DVP_DATA_FORMAT, attr->dvp.data_fmt);
    else if (attr->dvp.data_fmt == DVP_YUV422)
        set_bit_field(&vic_input_dvp, DVP_DATA_FORMAT, 6); // YUV422(8bit IO)

    if (is_output_y8(index, attr))  // 不改变四个raw8的先后顺序
        yuv_data_order = order_1_2_3_4;

    set_bit_field(&vic_input_dvp, YUV_DATA_ORDER, yuv_data_order);
    set_bit_field(&vic_input_dvp, DVP_TIMING_MODE, attr->dvp.timing_mode);
    set_bit_field(&vic_input_dvp, HSYNC_POLAR, attr->dvp.hsync_polarity);
    set_bit_field(&vic_input_dvp, VSYNC_POLAR, attr->dvp.vsync_polarity);
    set_bit_field(&vic_input_dvp, INTERLACE_EN, attr->dvp.img_scan_mode);

    if (attr->dvp.gpio_mode == DVP_PA_HIGH_8BIT ||
            attr->dvp.gpio_mode == DVP_PA_HIGH_10BIT)
        set_bit_field(&vic_input_dvp, DVP_RAW_ALIGN, 1);
    else
        set_bit_field(&vic_input_dvp, DVP_RAW_ALIGN, 0);

    vic_write_reg(index, VIC_INPUT_DVP, vic_input_dvp);

    unsigned long vic_ctrl_delay = 0;
    set_bit_field(&vic_ctrl_delay, VC_CONTROL_delay_hdelay, 1);
    set_bit_field(&vic_ctrl_delay, VC_CONTROL_delay_vdelay, 1);
    vic_write_reg(index, VIC_CONTROL_DELAY, vic_ctrl_delay);
}


static void vic_init_mipi_timing(int index, struct sensor_attr *attr)
{
    unsigned long horizontal_resolution;
    if (!attr->mipi.mipi_crop.enable) {
        horizontal_resolution = attr->sensor_info.width;
    } else {
        horizontal_resolution = attr->mipi.mipi_crop.output_width;
    }

    if (is_output_y8(index, attr)) {
        vic_write_reg(index, VIC_INPUT_MIPI, MIPI_YUV422);
        horizontal_resolution /= 2;
    } else
        vic_write_reg(index, VIC_INPUT_MIPI, attr->mipi.data_fmt);

    int width_4byte;
    int pixel_wdith;

    switch (attr->mipi.data_fmt) {
    case MIPI_RAW8:
        pixel_wdith = 8;
        break;
    case MIPI_RAW10:
        pixel_wdith = 10;
        break;
    case MIPI_RAW12:
        pixel_wdith = 12;
        break;
    default:
        pixel_wdith = 8;
        break;
    }

    /* 每行前有0个无效像素, 每行之后有0个无效像素 */
    width_4byte = ((horizontal_resolution + 0 + 0) * pixel_wdith + 31) / 32;
    vic_write_reg(index, MIPI_ALL_WIDTH_4BYTE, width_4byte);

    unsigned long hcrop_ch0 = 0;
    set_bit_field(&hcrop_ch0, MIPI_HCROP_CHO_all_image_width, horizontal_resolution);
    if (!attr->mipi.mipi_crop.enable) {
        set_bit_field(&hcrop_ch0, MIPI_HCROP_CHO_start_pixel, 0);
        vic_write_reg(index, MIPI_HCROP_CH0, hcrop_ch0);
    } else {
        unsigned long sensor_control = 0;
        set_bit_field(&sensor_control, MIPI_VCOMP_EN, attr->mipi.mipi_crop.sensor_ctrl.mipi_vcomp_en);
        set_bit_field(&sensor_control, MIPI_HCOMP_EN, attr->mipi.mipi_crop.sensor_ctrl.mipi_hcomp_en);
        set_bit_field(&sensor_control, LINE_SYNC_MODE, attr->mipi.mipi_crop.sensor_ctrl.line_sync_mode);
        set_bit_field(&sensor_control, WORK_START_FLAG, attr->mipi.mipi_crop.sensor_ctrl.work_start_flag);
        set_bit_field(&sensor_control, DATA_TYPE_EN, attr->mipi.mipi_crop.sensor_ctrl.data_type_en);
        set_bit_field(&sensor_control, DATA_TYPE_VALUE, attr->mipi.mipi_crop.sensor_ctrl.data_type_value);
        set_bit_field(&sensor_control, DEL_START, attr->mipi.mipi_crop.sensor_ctrl.del_start);
        vic_write_reg(index, MIPI_SENSOR_CONTROL, sensor_control);

        set_bit_field(&hcrop_ch0, MIPI_HCROP_CHO_start_pixel, attr->mipi.mipi_crop.hcrop_start);
        vic_write_reg(index, MIPI_HCROP_CH0, hcrop_ch0);

        unsigned long vcrop_ch0 = 0;
        set_bit_field(&vcrop_ch0, MIPI_VCROP_CHO_start_pixel, attr->mipi.mipi_crop.vcrop_start);
        vic_write_reg(index, MIPI_VCROP_DEL01, vcrop_ch0);

    }

    unsigned long vic_ctrl_delay = 0;
    set_bit_field(&vic_ctrl_delay, VC_CONTROL_delay_hdelay, 10);
    set_bit_field(&vic_ctrl_delay, VC_CONTROL_delay_vdelay, 10);
    vic_write_reg(index, VIC_CONTROL_DELAY, vic_ctrl_delay);
}

static void vic_init_common_setting(int index, struct sensor_attr *attr)
{
    unsigned long resolution = 0;
    unsigned long horizontal_resolution;
    unsigned long vorizontal_resolution;

    if ( (attr->dbus_type == SENSOR_DATA_BUS_MIPI) && (attr->mipi.mipi_crop.enable) ) {
        horizontal_resolution = attr->mipi.mipi_crop.output_width;
        vorizontal_resolution =  attr->mipi.mipi_crop.output_height;
    } else {
        horizontal_resolution = attr->sensor_info.width;
        vorizontal_resolution =  attr->sensor_info.height;
    }

    /*
     * sensor输出的图像数据是raw8的，但我们是使用yuv422的格式输入和输出的，
     * 因为raw8一个像素1个字节 yuv422一个像素占2个字节。
     * 所以填入寄存器的像素点为raw8像素点的1/2。
    */
    if (is_output_y8(index, attr))
        horizontal_resolution /= 2;

    set_bit_field(&resolution, HORIZONTAL_RESOLUTION, horizontal_resolution);
    set_bit_field(&resolution, VERTICAL_RESOLUTION, vorizontal_resolution);
    vic_write_reg(index ,VIC_RESOLUTION, resolution);

    int vic_interface = 0;
    switch (attr->dbus_type) {
    case SENSOR_DATA_BUS_BT656:
        vic_interface = 0;
        break;
    case SENSOR_DATA_BUS_BT601:
        vic_interface = 1;
        break;
    case SENSOR_DATA_BUS_MIPI:
        vic_interface = 2;
        break;
    case SENSOR_DATA_BUS_DVP:
        vic_interface = 3;
        break;
    case SENSOR_DATA_BUS_BT1120:
        vic_interface = 4;
        break;
    default:
        printf("vic%d unknown dbus_type: %d\n",  index, attr->dbus_type);
    }
    vic_write_reg(index ,VIC_INPUT_INTF, vic_interface);
}

static void init_dvp_dma(int index, struct sensor_attr *attr)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];
    unsigned long dma_resolution = 0;
    unsigned long horizontal_resolution = attr->sensor_info.width;

    if (is_output_y8(index, attr))
        horizontal_resolution /= 2;

    set_bit_field(&dma_resolution, DMA_HORIZONTAL_RESOLUTION, horizontal_resolution);
    set_bit_field(&dma_resolution, DMA_VERTICAL_RESOLUTION, attr->sensor_info.height);
    vic_write_reg(index ,VIC_DMA_RESOLUTION, dma_resolution);

    unsigned int base_mode = 0;
    unsigned int y_stride = 0;
    unsigned int uv_stride = 0;
    unsigned int horizon_time = 0;

    switch (attr->dvp.data_fmt) {
    case DVP_RAW8:
    case DVP_RAW10:
    case DVP_RAW12:
        base_mode = 0;
        y_stride = attr->sensor_info.width * 2;
        horizon_time = attr->sensor_info.width;
        if (is_output_y8(index, attr)) {
            base_mode = 3;
            y_stride = attr->sensor_info.width;
        }
        break;

    case DVP_YUV422:
        if (attr->info.data_fmt == CAMERA_PIX_FMT_GREY) {
            base_mode = 6;
            y_stride = attr->sensor_info.width;
        } else if (attr->info.data_fmt == CAMERA_PIX_FMT_NV12) {
            base_mode = 6;
            uv_stride = attr->sensor_info.width;
            y_stride = attr->sensor_info.width;
        } else if (attr->info.data_fmt == CAMERA_PIX_FMT_NV21) {
            base_mode = 7;
            uv_stride = attr->sensor_info.width;
            y_stride = attr->sensor_info.width;
        } else {
            base_mode = 3;
            y_stride = attr->sensor_info.width * 2;
        }

        horizon_time = attr->sensor_info.width * 2;
        break;

    default:
        break;
    }

    vic_set_bit(index, VIC_IN_HOR_PARA0, HACT_NUM, horizon_time);
    vic_write_reg(index, VIC_DMA_Y_CH_LINE_STRIDE, y_stride);
    vic_write_reg(index, VIC_DMA_UV_CH_LINE_STRIDE, uv_stride);

    unsigned long dma_configure = vic_read_reg(index ,VIC_DMA_CONFIGURE);
    set_bit_field(&dma_configure, Dma_en, 1);
    set_bit_field(&dma_configure, Buffer_number, 2 - 1);
    set_bit_field(&dma_configure, Base_mode, base_mode);
    set_bit_field(&dma_configure, Yuv422_order, 2);
    vic_write_reg(index ,VIC_DMA_CONFIGURE, dma_configure);

    /* default DMA Route */
    vic_data_path_select_route(index, drv->is_isp_enable);
}

static void init_mipi_dma(int index, struct sensor_attr *attr)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];
    unsigned long dma_resolution = 0;

    unsigned long horizontal_resolution;
    unsigned long vorizontal_resolution;

    if (!attr->mipi.mipi_crop.enable) {
        horizontal_resolution = attr->sensor_info.width;
        vorizontal_resolution = attr->sensor_info.height;
    } else {
        horizontal_resolution = attr->mipi.mipi_crop.output_width;
        vorizontal_resolution = attr->mipi.mipi_crop.output_height;
    }

    if (is_output_y8(index, attr))
        horizontal_resolution /= 2;

    set_bit_field(&dma_resolution, DMA_HORIZONTAL_RESOLUTION, horizontal_resolution);
    set_bit_field(&dma_resolution, DMA_VERTICAL_RESOLUTION, vorizontal_resolution);
    vic_write_reg(index ,VIC_DMA_RESOLUTION, dma_resolution);

    unsigned int base_mode = 0;
    unsigned int y_stride = 0;
    unsigned int uv_stride = 0;
    unsigned int yuv422_order_mode = 0;

    switch (attr->mipi.data_fmt) {
    case MIPI_RAW8:
    case MIPI_RAW10:
    case MIPI_RAW12:
        base_mode = 0;
        y_stride = horizontal_resolution * 2;

        if (is_output_y8(index, attr)) {
            base_mode = 3;          /* YUV422 packey */
            yuv422_order_mode = 3;  /* =3, U1Y1V1Y2 */
            y_stride = horizontal_resolution * 2;
        }
        break;

    case MIPI_YUV422:
        if (attr->info.data_fmt == CAMERA_PIX_FMT_GREY) {
            base_mode = 6;
            y_stride = attr->info.width;
        } else if (attr->info.data_fmt == CAMERA_PIX_FMT_NV12) {
            base_mode = 6;
            uv_stride = attr->info.width;
            y_stride = attr->info.width;
        } else if (attr->info.data_fmt == CAMERA_PIX_FMT_NV21) {
            base_mode = 7;
            uv_stride = attr->info.width;
            y_stride = attr->info.width;
        } else {
            base_mode = 3;
            y_stride = attr->info.width * 2;
        }
        break;

    default:
        break;
    }

    vic_write_reg(index ,VIC_DMA_Y_CH_LINE_STRIDE, y_stride);
    vic_write_reg(index ,VIC_DMA_UV_CH_LINE_STRIDE, uv_stride);

    unsigned long dma_configure = vic_read_reg(index, VIC_DMA_CONFIGURE);
    set_bit_field(&dma_configure, Dma_en, 1);
    set_bit_field(&dma_configure, Buffer_number, 2 - 1);
    set_bit_field(&dma_configure, Base_mode, base_mode);
    set_bit_field(&dma_configure, Yuv422_order, yuv422_order_mode);

    vic_write_reg(index ,VIC_DMA_CONFIGURE, dma_configure);

    /* default DMA Route */
    vic_data_path_select_route(index, drv->is_isp_enable);
}

static void init_dvp_irq(int index)
{
    unsigned long vic_int_mask = 0;

    set_bit_field(&vic_int_mask, VIC_FRM_START, 1);
    set_bit_field(&vic_int_mask, VIC_FRM_RST, 1);

    vic_write_reg(index, VIC_INT_CLR, vic_int_mask);
    vic_write_reg(index, VIC_INT_MASK, vic_int_mask);
}

static void init_mipi_irq(int index)
{
    unsigned long vic_int_mask = 0xFFFFF;

    set_bit_field(&vic_int_mask, VIC_DONE, 0);
    set_bit_field(&vic_int_mask, MIPI_VCOMP_ERR_CH0, 0);
    set_bit_field(&vic_int_mask, MIPI_HCOMP_ERR_CH0, 0);
    set_bit_field(&vic_int_mask, DMA_FRD, 0);
    set_bit_field(&vic_int_mask, VIC_HVRES_ERR, 0);
    set_bit_field(&vic_int_mask, VIC_FRM_START, 0);
    set_bit_field(&vic_int_mask, VIC_FRD, 0);

    vic_write_reg(index, VIC_INT_CLR, vic_int_mask);
    vic_write_reg(index, VIC_INT_MASK, vic_int_mask);
}

static int vic_isp_div_clock_enable(int index)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];

    if ( !clk_is_enabled(drv->isp_div) ) {
        clk_set_rate(drv->isp_div, drv->isp_clk_rate);

    } else  if (drv->isp_clk_rate != clk_get_rate(drv->isp_div)) {
        printf("vic%d already enable isp clock(%ld) not change to %ld\n",  \
                !index, clk_get_rate(drv->isp_div), drv->isp_clk_rate);
    }

    clk_enable(drv->isp_div);

    return 0;
}

static int vic_isp_div_clock_disable(int index)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];

    clk_disable(drv->isp_div);

    return 0;
}

void vic_dump_reg(int index)
{
    printf("==========dump vic%d register============\n", index);

    printf("VIC_CONTROL                 : 0x%08x\n", vic_read_reg(index, VIC_CONTROL));
    printf("VIC_RESOLUTION              : 0x%08x\n", vic_read_reg(index, VIC_RESOLUTION));
    printf("VIC_FRM_ECC                 : 0x%08x\n", vic_read_reg(index, VIC_FRM_ECC));
    printf("VIC_INPUT_INTF              : 0x%08x\n", vic_read_reg(index, VIC_INPUT_INTF));
    printf("VIC_INPUT_DVP               : 0x%08x\n", vic_read_reg(index, VIC_INPUT_DVP));
    printf("VIC_INPUT_MIPI              : 0x%08x\n", vic_read_reg(index, VIC_INPUT_MIPI));
    printf("VIC_IN_HOR_PARA0            : 0x%08x\n", vic_read_reg(index, VIC_IN_HOR_PARA0));
    printf("VIC_IN_HOR_PARA1            : 0x%08x\n", vic_read_reg(index, VIC_IN_HOR_PARA1));
    printf("VIC_BK_CB_CTRL              : 0x%08x\n", vic_read_reg(index, VIC_BK_CB_CTRL));
    printf("VIC_BK_CB_BLK               : 0x%08x\n", vic_read_reg(index, VIC_BK_CB_BLK));
    printf("VIC_IN_VER_PARA0            : 0x%08x\n", vic_read_reg(index, VIC_INPUT_VPARA0));
    printf("VIC_IN_VER_PARA1            : 0x%08x\n", vic_read_reg(index, VIC_INPUT_VPARA1));
    printf("VIC_IN_VER_PARA2            : 0x%08x\n", vic_read_reg(index, VIC_INPUT_VPARA2));
    printf("VIC_IN_VER_PARA3            : 0x%08x\n", vic_read_reg(index, VIC_INPUT_VPARA3));
    printf("VIC_VLD_LINE_SAV            : 0x%08x\n", vic_read_reg(index, VIC_VLD_LINE_SAV));
    printf("VIC_VLD_LINE_EAV            : 0x%08x\n", vic_read_reg(index, VIC_VLD_LINE_EAV));
    printf("VIC_VLD_FRM_SAV             : 0x%08x\n", vic_read_reg(index, VIC_VLD_FRM_SAV));
    printf("VIC_VLD_FRM_EAV             : 0x%08x\n", vic_read_reg(index, VIC_VLD_FRM_EAV));
    printf("VIC_VC_CONTROL_FSM          : 0x%08x\n", vic_read_reg(index, VIC_VC_CONTROL_FSM));
    printf("VIC_VC_CONTROL_CH0_PIX      : 0x%08x\n", vic_read_reg(index, VIC_VC_CONTROL_CH0_PIX));
    printf("VIC_VC_CONTROL_CH1_PIX      : 0x%08x\n", vic_read_reg(index, VIC_VC_CONTROL_CH1_PIX));
    printf("VIC_VC_CONTROL_CH2_PIX      : 0x%08x\n", vic_read_reg(index, VIC_VC_CONTROL_CH2_PIX));
    printf("VIC_VC_CONTROL_CH3_PIX      : 0x%08x\n", vic_read_reg(index, VIC_VC_CONTROL_CH3_PIX));
    printf("VIC_VC_CONTROL_CH0_LINE     : 0x%08x\n", vic_read_reg(index, VIC_VC_CONTROL_CH0_LINE));
    printf("VIC_VC_CONTROL_CH1_LINE     : 0x%08x\n", vic_read_reg(index, VIC_VC_CONTROL_CH1_LINE));
    printf("VIC_VC_CONTROL_CH2_LINE     : 0x%08x\n", vic_read_reg(index, VIC_VC_CONTROL_CH2_LINE));
    printf("VIC_VC_CONTROL_CH3_LINE     : 0x%08x\n", vic_read_reg(index, VIC_VC_CONTROL_CH3_LINE));
    printf("VIC_VC_CONTROL_FIFO_USE     : 0x%08x\n", vic_read_reg(index, VIC_VC_CONTROL_FIFO_USE));
    printf("VIC_CB_1ST                  : 0x%08x\n", vic_read_reg(index, VIC_CB_1ST));
    printf("VIC_CB_2ND                  : 0x%08x\n", vic_read_reg(index, VIC_CB_2ND));
    printf("VIC_CB_3RD                  : 0x%08x\n", vic_read_reg(index, VIC_CB_3RD));
    printf("VIC_CB_4TH                  : 0x%08x\n", vic_read_reg(index, VIC_CB_4TH));
    printf("VIC_CB_5TH                  : 0x%08x\n", vic_read_reg(index, VIC_CB_5TH));
    printf("VIC_CB_6TH                  : 0x%08x\n", vic_read_reg(index, VIC_CB_6TH));
    printf("VIC_CB_7TH                  : 0x%08x\n", vic_read_reg(index, VIC_CB_7TH));
    printf("VIC_CB_8TH                  : 0x%08x\n", vic_read_reg(index, VIC_CB_8TH));
    printf("VIC_CB2_1ST                 : 0x%08x\n", vic_read_reg(index, VIC_CB2_1ST));
    printf("VIC_CB2_2ND                 : 0x%08x\n", vic_read_reg(index, VIC_CB2_2ND));
    printf("VIC_CB2_3RD                 : 0x%08x\n", vic_read_reg(index, VIC_CB2_3RD));
    printf("VIC_CB2_4TH                 : 0x%08x\n", vic_read_reg(index, VIC_CB2_4TH));
    printf("VIC_CB2_5TH                 : 0x%08x\n", vic_read_reg(index, VIC_CB2_5TH));
    printf("VIC_CB2_6TH                 : 0x%08x\n", vic_read_reg(index, VIC_CB2_6TH));
    printf("VIC_CB2_7TH                 : 0x%08x\n", vic_read_reg(index, VIC_CB2_7TH));
    printf("VIC_CB2_8TH                 : 0x%08x\n", vic_read_reg(index, VIC_CB2_8TH));
    printf("MIPI_ALL_WIDTH_4BYTE        : 0x%08x\n", vic_read_reg(index, MIPI_ALL_WIDTH_4BYTE));
    printf("MIPI_VCROP_DEL01            : 0x%08x\n", vic_read_reg(index, MIPI_VCROP_DEL01));
    printf("MIPI_SENSOR_CONTROL         : 0x%08x\n", vic_read_reg(index, MIPI_SENSOR_CONTROL));
    printf("MIPI_HCROP_CH0              : 0x%08x\n", vic_read_reg(index, MIPI_HCROP_CH0));
    printf("MIPI_VCROP_SHADOW_CFG       : 0x%08x\n", vic_read_reg(index, MIPI_VCROP_SHADOW_CFG));
    printf("VIC_CONTROL_LIMIT           : 0x%08x\n", vic_read_reg(index, VIC_CONTROL_LIMIT));
    printf("VIC_CONTROL_DELAY           : 0x%08x\n", vic_read_reg(index, VIC_CONTROL_DELAY));
    printf("VIC_CONTROL_TIZIANO_ROUTE   : 0x%08x\n", vic_read_reg(index, VIC_CONTROL_TIZIANO_ROUTE));
    printf("VIC_CONTROL_DMA_ROUTE       : 0x%08x\n", vic_read_reg(index, VIC_CONTROL_DMA_ROUTE));
    printf("VIC_INT_STA                 : 0x%08x\n", vic_read_reg(index, VIC_INT_STA));
    printf("VIC_INT_MASK                : 0x%08x\n", vic_read_reg(index, VIC_INT_MASK));
    printf("VIC_INT_CLR                 : 0x%08x\n", vic_read_reg(index, VIC_INT_CLR));

    printf("VIC_DMA_CONFIGURE           : 0x%08x\n", vic_read_reg(index, VIC_DMA_CONFIGURE));
    printf("VIC_DMA_RESOLUTION          : 0x%08x\n", vic_read_reg(index, VIC_DMA_RESOLUTION));
    printf("VIC_DMA_RESET               : 0x%08x\n", vic_read_reg(index, VIC_DMA_RESET));
    printf("DMA_Y_CH_LINE_STRIDE        : 0x%08x\n", vic_read_reg(index, VIC_DMA_Y_CH_LINE_STRIDE));
    printf("VIC_DMA_Y_CH_BUF0_ADDR      : 0x%08x\n", vic_read_reg(index, VIC_DMA_Y_CH_BUF0_ADDR));
    printf("VIC_DMA_Y_CH_BUF1_ADDR      : 0x%08x\n", vic_read_reg(index, VIC_DMA_Y_CH_BUF1_ADDR));
    printf("VIC_DMA_Y_CH_BUF2_ADDR      : 0x%08x\n", vic_read_reg(index, VIC_DMA_Y_CH_BUF2_ADDR));
    printf("VIC_DMA_Y_CH_BUF3_ADDR      : 0x%08x\n", vic_read_reg(index, VIC_DMA_Y_CH_BUF3_ADDR));
    printf("VIC_DMA_Y_CH_BUF4_ADDR      : 0x%08x\n", vic_read_reg(index, VIC_DMA_Y_CH_BUF4_ADDR));
    printf("VIC_DMA_UV_CH_LINE_STRIDE   : 0x%08x\n", vic_read_reg(index, VIC_DMA_UV_CH_LINE_STRIDE));
    printf("VIC_DMA_UV_CH_BUF0_ADDR     : 0x%08x\n", vic_read_reg(index, VIC_DMA_UV_CH_BUF0_ADDR));
    printf("VIC_DMA_UV_CH_BUF1_ADDR     : 0x%08x\n", vic_read_reg(index, VIC_DMA_UV_CH_BUF1_ADDR));
    printf("VIC_DMA_UV_CH_BUF2_ADDR     : 0x%08x\n", vic_read_reg(index, VIC_DMA_UV_CH_BUF2_ADDR));
    printf("VIC_DMA_UV_CH_BUF3_ADDR     : 0x%08x\n", vic_read_reg(index, VIC_DMA_UV_CH_BUF3_ADDR));
    printf("VIC_DMA_UV_CH_BUF4_ADDR     : 0x%08x\n", vic_read_reg(index, VIC_DMA_UV_CH_BUF4_ADDR));
    printf("=========================================\n");
}

static void vic_dvp_init_setting(int index, struct sensor_attr *attr)
{
    assert_range(attr->sensor_info.width, 1, 2048);
    assert_range(attr->sensor_info.height, 1, 2048);
    assert_range(attr->dvp.data_fmt, DVP_RAW8, DVP_YUV422);
    assert(attr->dbus_type == SENSOR_DATA_BUS_DVP);
    assert(attr->dvp.timing_mode == DVP_HREF_MODE);

    vic_reset(index);

    vic_init_common_setting(index, attr);

    vic_init_dvp_timing(index, attr);

    vic_register_enable(index);

    vic_dma_reset(index);

    init_dvp_dma(index, attr);

    init_dvp_irq(index);

    vic_start(index);

    //vic_dump_reg(index);
}

static void vic_mipi_init_setting(int index, struct sensor_attr *attr)
{
    assert_range(attr->sensor_info.width, 1, 3840);
    assert_range(attr->sensor_info.height, 1, 2560);
    assert_range(attr->mipi.data_fmt, MIPI_RAW8, MIPI_YUV422);
    assert_range(attr->mipi.lanes, 1, 4);
    assert(attr->dbus_type == SENSOR_DATA_BUS_MIPI);

    vic_reset(index);

    vic_init_common_setting(index, attr);

    vic_init_mipi_timing(index, attr);

    vic_dma_reset(index);

    init_mipi_dma(index, attr);

    init_mipi_irq(index);

    int csi_ret = mipi_csi_phy_initialization(index, &attr->mipi);
    assert(csi_ret >= 0);

    vic_register_enable(index);

    vic_start(index);

    //vic_dump_reg(index);
}

static void vic_hal_stream_on(int index, struct sensor_attr *attr)
{
    if (attr->dbus_type == SENSOR_DATA_BUS_DVP)
        vic_dvp_init_setting(index, attr);
    else if (attr->dbus_type == SENSOR_DATA_BUS_MIPI)
        vic_mipi_init_setting(index, attr);
}


static void vic_hal_stream_off(int index, struct sensor_attr *attr)
{
    vic_reset(index);
    usleep(1000);

    if (attr->dbus_type == SENSOR_DATA_BUS_MIPI)
        mipi_csi_phy_stop(index);
}

int vic_stream_on(int index, struct sensor_attr *attr)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];
    int ret = 0;

    /* 当前仅支持MIPI DVP */
    assert_range(attr->dbus_type, SENSOR_DATA_BUS_MIPI, SENSOR_DATA_BUS_DVP);

    mutex_lock(&drv->lock);

    if (!drv->camera.is_power_on) {
        printf("vic%d can't stream on when not power on\n", index);
        ret = -EINVAL;
        goto out;
    }

    if (attr->dbus_type == SENSOR_DATA_BUS_DVP) {

        ret = attr->ops.stream_on();
        if (ret)
            goto out;

        vic_hal_stream_on(index, attr);
    } else {

        vic_hal_stream_on(index, attr);

        ret = attr->ops.stream_on();
        if (ret) {
            vic_hal_stream_off(index, attr);
            goto out;
        }
    }

    drv->camera.is_stream_on = 1;

out:
    mutex_unlock(&drv->lock);
    return ret;
}

void vic_stream_off(int index, struct sensor_attr *attr)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];

    mutex_lock(&drv->lock);

    if (!drv->camera.is_stream_on) {
        printf("vic%d is already steam off\n", index);
        goto unlock;
    }

    vic_hal_stream_off(index, attr);

    attr->ops.stream_off();

    drv->camera.is_stream_on = 0;

unlock:
    mutex_unlock(&drv->lock);
}

int vic_power_on(int index)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];
    struct sensor_attr *attr = drv->camera.sensor;
    int ret = 0;

    mutex_lock(&drv->lock);

    if (drv->camera.is_power_on) {
        printf("vic%d is already power on, no need power on again\n", index);
        goto unlock;
    }

    /* enable clock */
    if (attr->isp_clk_rate)
        drv->isp_clk_rate = attr->isp_clk_rate;

    vic_isp_div_clock_enable(index);

    clk_enable(drv->isp_gate_clk);

    isp_clk_power_mode_enable(index);
    usleep(1500);

    /* device(sensor) power on */
    ret = attr->ops.power_on();
    if (!ret)
        drv->camera.is_power_on = 1;

unlock:
    mutex_unlock(&drv->lock);

    return ret;
}

void vic_power_off(int index)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];
    struct sensor_attr *attr = drv->camera.sensor;

    if (!drv->camera.is_power_on) {
        printf("vic%d is already power off\n", index);
        return ;
    }

    if (drv->camera.is_stream_on)
        vic_stream_off(index, drv->camera.sensor);

    mutex_lock(&drv->lock);

    /* disable clock */
    isp_clk_power_mode_disable(index);
    clk_disable(drv->isp_gate_clk);
    vic_isp_div_clock_disable(index);

    /* device(sensor) power off */
    attr->ops.power_off();
    drv->camera.is_power_on = 0;

    mutex_unlock(&drv->lock);
}

/*
 * VIC && device power state
 * return =1: is power on
 *        =0: is power off
 */
int vic_power_state(int index)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];

    return drv->camera.is_power_on;
}

/*
 * VIC && device stream state
 * return =1: is stream on
 *        =0: is stream off
 */
int vic_stream_state(int index)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];

    return drv->camera.is_stream_on;
}


static int jz_vic_resources_init(int index)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];
    int ret;

    ret = camera_mclk_gpio_init(drv->mclk_io);
    if(ret < 0) {
        printf("vic%d driver mclk gpio init failed\n", index);
        goto error_mclk_gpio_init;
    }

    drv->mclk_div = clk_get("cgu_cim");   /* VIC/CIM 可共用 */
    assert(drv->mclk_div);

    drv->mclk_gate = clk_get(drv->mclk_gate_clk_name);   /* VIC/CIM 可共用 */
    assert(drv->mclk_gate);

    drv->isp_div = clk_get(drv->isp_div_clk_name);
    assert(drv->isp_div);

    drv->isp_gate_clk = clk_get(drv->isp_gate_clk_name);
    assert(drv->isp_gate_clk);

    drv->isp_clk_rate = 90 * 1000 * 1000;

    mutex_init(&drv->lock);

    if (drv->is_isp_enable)
        ret = jz_vic_tiziano_drv_init(index);
    else
        ret = jz_vic_mem_drv_init(index);

    if (ret) {
        printf("camera: failed to init vic%d resources\n", index);
        goto error_vic_resources_init;
    }

    drv->is_finish = 1;

    return 0;

error_vic_resources_init:
    clk_put(drv->mclk_div);
    clk_put(drv->mclk_gate);
    clk_put(drv->isp_div);
    clk_put(drv->isp_gate_clk);
    camera_mclk_gpio_deinit(drv->mclk_io);
error_mclk_gpio_init:
    assert(ret >= 0);
    return ret;
}

static void jz_vic_resources_deinit(int index)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];

    drv->is_finish = 0;

    if (drv->is_isp_enable)
        jz_vic_tiziano_drv_deinit(index);
    else
        jz_vic_mem_drv_deinit(index);

    clk_put(drv->mclk_div);
    clk_put(drv->mclk_gate);
    clk_put(drv->isp_div);
    clk_put(drv->isp_gate_clk);
    camera_mclk_gpio_deinit(drv->mclk_io);
}

static int jz_cim_resources_init(int index)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];
    int ret;

    ret = camera_mclk_gpio_init(drv->mclk_io);
    if(ret < 0) {
        printf("cim driver mclk gpio init failed\n");
        return ret;
    }

    drv->mclk_div = clk_get("cgu_cim");   /* VIC/CIM 可共用 */
    assert(drv->mclk_div);

    drv->mclk_gate = clk_get(drv->mclk_gate_clk_name);   /* VIC/CIM 可共用 */
    assert(drv->mclk_gate);

    mutex_init(&drv->lock);

    ret = jz_cim_drv_init();
    if (ret) {
        printf("camera: failed to init cim resources\n");
        goto error_cim_resources_init;
    }

    drv->is_finish = 1;

    //printf("cim resources register successfully\n");

    return 0;

error_cim_resources_init:
    clk_put(drv->mclk_div);
    clk_put(drv->mclk_gate);
    camera_mclk_gpio_deinit(drv->mclk_io);

    return ret;
}

static void jz_cim_resources_deinit(int index)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];

    drv->is_finish = 0;

    jz_cim_drv_deinit();

    clk_put(drv->mclk_div);
    clk_put(drv->mclk_gate);
    camera_mclk_gpio_deinit(drv->mclk_io);
}


/*
 * 外部函数接口
 */

/*
 * VIC/CIM 模块公用mclk, 使用index参数方便获取mclk的变量
 */
void soc_vic_camera_enable_sensor_mclk(int index, unsigned long clk_rate)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];
    unsigned long rate = 0;

    if ( !clk_is_enabled(drv->mclk_div) ) {
        clk_set_rate(drv->mclk_div, clk_rate);
        clk_enable(drv->mclk_div);
        clk_enable(drv->mclk_gate);
        return ;
    }

    rate = clk_get_rate(drv->mclk_div);
    if (rate != clk_rate) {
        printf("mclk already enabled rate=%ld, not change to %ld\n", rate, clk_rate);
    }

    clk_enable(drv->mclk_div);
    clk_enable(drv->mclk_gate);
}

void soc_vic_camera_disable_sensor_mclk(int index)
{
    struct jz_camera_data *drv = &jz_camera_dev[index];

    clk_disable(drv->mclk_div);
    clk_disable(drv->mclk_gate);
}

int soc_vic_camera_register_sensor(int index, struct sensor_attr *sensor)
{
    assert(index < 3);

    struct jz_camera_data *drv = &jz_camera_dev[index];
    assert(drv->is_finish > 0);
    assert(!drv->camera.sensor);

    int ret = -EINVAL;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        if (drv->is_isp_enable)
            ret = soc_vic_register_sensor_tiziano_routine(index, sensor);
        else
            ret = soc_vic_register_sensor_mem_routine(index, drv->cam_mem_cnt, sensor);

        break;

    case 2:
        /* CIM */
        ret = soc_cim_register_sensor_routine(index, drv->cam_mem_cnt, sensor);
        break;

    default:
        printf("camera register index(%d) is invalid\n", index);
        break;
    }

    if (!ret) {
        drv->camera.sensor = sensor;
        drv->camera.is_power_on = 0;
        drv->camera.is_stream_on = 0;
    }

    return ret;
}

void soc_vic_camera_unregister_sensor(int index, struct sensor_attr *sensor)
{
    assert(index < 3);
    struct jz_camera_data *drv = &jz_camera_dev[index];
    assert(drv->is_finish > 0);
    assert(drv->camera.sensor);
    assert(sensor == drv->camera.sensor);

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        if (drv->is_isp_enable)
            soc_vic_unregister_sensor_tiziano_routine(index, sensor);
        else
            soc_vic_unregister_sensor_mem_routine(index, sensor);

        break;

    case 2:
        /* CIM */
        soc_cim_unregister_sensor_routine(index, sensor);
        break;

    default:
        printf("camera unregister index(%d) is invalid\n", index);
        break;
    }


    drv->camera.sensor = NULL;
}

/*
 * 应用 soc实现转换接口函数
 */
struct camera_device *soc_camera_hal_detect(int index)
{
    assert(index < 3);
    struct jz_camera_data *drv = &jz_camera_dev[index];
    struct camera_device *camera = NULL;
    int ret = -ENODEV;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        ret = soc_vic_chan_mem_detect(index);
        break;

    case 2:
        /* CIM */
        ret = soc_cim_detect(index);
        break;

    default:
        printf("camera index(%d) out of range[0~2]\n", index);
        break;
    }

    if (!ret)
        camera = &drv->camera;

    return camera;
}

static struct jz_camera_data *camera_get_jz_camera_data(struct camera_device *camera)
{
    assert(camera);
    struct jz_camera_data *drv = container_of(camera, struct jz_camera_data, camera);
    int index = drv->index;

    if (index < 0 || index > 2) {
        printf("camera index(%d) out of range[0~2]\n", index);
        goto error;
    }

    if (drv == &jz_camera_dev[index])
        return drv;

error:
    panic("camera device handler is invalid. please check parameter\n");
}

struct camera_info *soc_camera_hal_get_info(struct camera_device *camera)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        return soc_vic_chan_mem_get_info(index);

    case 2:
        /* CIM */
        return soc_cim_get_info(index);
        break;
    }

    return NULL;
}

int soc_camera_hal_power_on(struct camera_device *camera)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;
    int ret = -ENODEV;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        ret = soc_vic_chan_mem_power_on(index);
        break;

    case 2:
        /* CIM */
        ret = soc_cim_power_on(index);
        break;
    }

    return ret;
}

void soc_camera_hal_power_off(struct camera_device *camera)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        soc_vic_chan_mem_power_off(index);
        break;

    case 2:
        /* CIM */
        soc_cim_power_off(index);
        break;
    }
}

int soc_camera_hal_stream_on(struct camera_device *camera)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;
    int ret = -ENODEV;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        ret = soc_vic_chan_mem_stream_on(index);
        break;

    case 2:
        /* CIM */
        ret = soc_cim_stream_on(index);
        break;
    }

    return ret;
}

void soc_camera_hal_stream_off(struct camera_device *camera)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        soc_vic_chan_mem_stream_off(index);
        break;

    case 2:
        /* CIM */
        soc_cim_stream_off(index);
        break;
    }
}

camera_frame_error_type soc_camera_hal_get_frame_error(struct camera_device *camera)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;
    int ret = -ENODEV;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        ret = soc_vic_chan_mem_get_frame_error(index);
        break;

    case 2:
        /* CIM */
        ret = soc_cim_get_frame_error(index);
        break;
    }

    return ret;
}

void *soc_camera_hal_wait_frame(struct camera_device *camera)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;
    void *mem = NULL;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        mem = soc_vic_chan_mem_wait_frame(index);
        break;

    case 2:
        /* CIM */
        mem = soc_cim_wait_frame(index);
        break;
    }

    return mem;
}

int soc_camera_hal_put_frame(struct camera_device *camera, void *buf)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;
    int ret = 0;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        ret = soc_vic_chan_mem_put_frame(index, buf);
        break;

    case 2:
        /* CIM */
        soc_cim_put_frame(index, buf);
        break;
    }

    return ret;
}

void *soc_camera_hal_get_frame(struct camera_device *camera)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;
    void *mem = NULL;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        mem = soc_vic_chan_mem_get_frame(index);
        break;

    case 2:
        /* CIM */
        mem = soc_cim_get_frame(index);
        break;
    }

    return mem;
}

int soc_camera_hal_dqbuf(struct camera_device *camera, struct frame_info *frame)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;
    int ret =0 ;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        ret = soc_vic_chan_mem_dqbuf(index, frame);
        break;

    case 2:
        /* CIM */
        ret = soc_cim_dqbuf(index, frame);
        break;
    }

    return ret;
}

int soc_camera_hal_dqbuf_wait(struct camera_device *camera, struct frame_info *frame)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;
    int ret = 0;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        ret = soc_vic_chan_mem_dqbuf_wait(index, frame);
        break;

    case 2:
        /* CIM */
        ret = soc_cim_dqbuf_wait(index, frame);
        break;
    }

    return ret;
}

int soc_camera_hal_qbuf(struct camera_device *camera, struct frame_info *frame)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;
    int ret = 0;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        ret = soc_vic_chan_mem_qbuf(index, frame);
        break;

    case 2:
        /* CIM */
        ret = soc_cim_qbuf(index, frame);
        break;
    }

    return ret;
}

int soc_camera_set_hal_sensor_reg(struct camera_device *camera, struct sensor_dbg_register *reg)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;
    int ret = 0;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        ret = soc_vic_chan_set_hal_sensor_reg(index, reg);
        break;

    case 2:
        /* CIM */
        ret = soc_cim_set_hal_sensor_reg(index, reg);
        break;
    }

    return ret;
}

int soc_camera_get_hal_sensor_reg(struct camera_device *camera, struct sensor_dbg_register *reg)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;
    int ret = 0;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        ret = soc_vic_chan_get_hal_sensor_reg(index, reg);
        break;

    case 2:
        /* CIM */
        ret = soc_cim_get_hal_sensor_reg(index, reg);
        break;
    }

    return ret;
}

unsigned int soc_camera_hal_get_available_frame_count(struct camera_device *camera)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;
    int ret = 0;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        ret = soc_vic_chan_mem_get_available_frame_count(index);
        break;

    case 2:
        /* CIM */
        ret = soc_cim_get_available_frame_count(index);
        break;
    }

    return ret;
}

void soc_camera_hal_skip_frames(struct camera_device *camera, unsigned int frames)
{
    struct jz_camera_data *drv = camera_get_jz_camera_data(camera);
    int index = drv->index;

    switch (index) {
    case 0:
    case 1:
        /* VIC */
        soc_vic_chan_mem_skip_frames(index, frames);
        break;

    case 2:
        /* CIM */
        soc_cim_skip_frames(index, frames);
        break;
    }
}



int jz_arch_vic_init(void)
{
    /* VIC */
    if (jz_camera_dev[0].is_enable)
        jz_vic_resources_init(jz_camera_dev[0].index);

    if (jz_camera_dev[1].is_enable)
        jz_vic_resources_init(jz_camera_dev[1].index);

    /* CIM */
    if (jz_camera_dev[2].is_enable)
        jz_cim_resources_init(jz_camera_dev[2].index);

    return 0;
}

__attribute__((__unused__)) void jz_arch_vic_exit(void)
{
    /* VIC */
    if (jz_camera_dev[0].is_finish)
        jz_vic_resources_deinit(jz_camera_dev[0].index);

    if (jz_camera_dev[1].is_finish)
        jz_vic_resources_deinit(jz_camera_dev[1].index);

    /* CIM */
    if (jz_camera_dev[2].is_finish)
        jz_cim_resources_deinit(jz_camera_dev[2].index);
}
