#ifndef __ISP_TUNING_X2000_H__
#define __ISP_TUNING_X2000_H__

#include <stdint.h>
#include <driver.h>


typedef struct handle_desc isp_tuning_hd_t;


/**
 * ISP功能开关
 */
typedef enum isp_core_module_ops_mode{
    ISP_TUNING_OPS_MODE_DISABLE,            /**< 不使能该模块功能 */
    ISP_TUNING_OPS_MODE_ENABLE,            /**< 使能该模块功能 */
    ISP_TUNING_OPS_MODE_BUTT,            /**< 用于判断参数的有效性，参数大小必须小于这个值 */
} isp_tuning_ops_mode;

/**
 * ISP功能选用开关
 */
typedef enum isp_core_module_ops_type{
    ISP_TUNING_OPS_TYPE_AUTO,            /**< 该模块的操作为自动模式 */
    ISP_TUNING_OPS_TYPE_MANUAL,            /**< 该模块的操作为手动模式 */
    ISP_TUNING_OPS_TYPE_BUTT,            /**< 用于判断参数的有效性，参数大小必须小于这个值 */
} isp_tuning_ops_type;

/**
 * ISP Module Control
 */
typedef union {
    unsigned int key;
    struct {
        unsigned int bitBypassDPC : 1; /* [0]  */
        unsigned int bitBypassGIB : 1; /* [1]  */
        unsigned int bitBypassLSC : 1; /* [2]  */
        unsigned int bitBypassAWB : 1; /* [3]  */
        unsigned int bitBypassADR : 1; /* [4]  */
        unsigned int bitBypassDMSC : 1; /* [5]    */
        unsigned int bitBypassCCM : 1; /* [6]  */
        unsigned int bitBypassGAMMA : 1; /* [7]     */
        unsigned int bitBypassDEFOG : 1; /* [8]     */
        unsigned int bitBypassCLM : 1; /* [9]  */
        unsigned int bitBypassYSHARPEN : 1; /* [10]  */
        unsigned int bitBypassMDNS : 1; /* [11]     */
        unsigned int bitBypassSDNS : 1; /* [12]     */
        unsigned int bitBypassHLDC : 1; /* [13]     */
        unsigned int bitBypassTP : 1; /* [14]  */
        unsigned int bitBypassFONT : 1; /* [15]     */
        unsigned int bitRsv : 15; /* [16 ~ 30]    */
        unsigned int bitRsv2 : 1; /* [31]  */
    };
} isp_module_ctrl;

/**
 * ISP 工作模式配置，正常模式或夜视模式。
 */
typedef enum isp_core_mode_day_and_night{
    ISP_RUNNING_MODE_DAY = 0,                /**< 正常模式 */
    ISP_RUNNING_MODE_NIGHT = 1,                /**< 夜视模式 */
    ISP_RUNNING_MODE_BUTT,                    /**< 最大值 */
} isp_running_mode;

/**
 * ISP抗闪频属性参数结构体。
 */
typedef enum {
    ISP_ANTIFLICKER_DISABLE = 0,    /**< 不使能ISP抗闪频功能 */
    ISP_ANTIFLICKER_50HZ = 50,  /**< 使能ISP抗闪频功能, 并设置频率为50HZ */
    ISP_ANTIFLICKER_60HZ = 60,  /**< 使能ISP抗闪频功能，并设置频率为60HZ */
} isp_anti_flicker_attr;

/**
 * ISP EV 参数。
 */
typedef struct isp_core_ev_attr{
    uint32_t ev;            /**< 曝光值 */
    uint32_t expr_us;        /**< 曝光时间 */
    uint32_t ev_log2;        /**<log格式曝光时间 */
    uint32_t again;            /**< 模拟增益 */
    uint32_t dgain;            /**< 数字增益 */
    uint32_t gain_log2;        /**< log格式增益 */
} isp_ev_attr;

/**
 * 曝光模式
 */
enum isp_core_expr_mode {
    ISP_CORE_EXPR_MODE_AUTO,            /**< 自动模式 */
    ISP_CORE_EXPR_MODE_MANUAL,            /**< 手动模式 */
};

/**
 * 曝光单位
 */
enum isp_core_expr_unit {
    ISP_CORE_EXPR_UNIT_LINE,            /**< 行 */
    ISP_CORE_EXPR_UNIT_US,                /**< 微秒 */
};

/**
 * 曝光参数
 */
typedef union isp_core_expr_attr{
    struct {
        enum isp_core_expr_mode mode;        /**< 设置的曝光模式 */
        enum isp_core_expr_unit unit;        /**< 设置的曝光单位 */
        unsigned int time;
    } s_attr;
    struct {
        enum isp_core_expr_mode mode;            /**< 获取的曝光模式 */
        unsigned short integration_time;        /**< 获取的曝光时间，单位为行 */
        unsigned short integration_time_min;    /**< 获取的曝光最小时间，单位为行 */
        unsigned short integration_time_max;    /**< 获取的曝光最大时间，单位为行 */
        unsigned short one_line_expr_in_us;        /**< 获取的一行曝光时间对应的微妙数 */
    } g_attr;
} isp_expr;

/**
 * AE Min
 */
typedef struct {
    unsigned int min_it;  /**< AE最小曝光 */
    unsigned int min_again;     /**< AE 最小模拟增益 */
} isp_ae_min;

/**
* 权重信息
*/
typedef struct isp_core_weight_attr{
    unsigned char weight[15][15];    /**< 各区域权重信息 [0 ~ 8]*/
} isp_weight;

typedef struct {
    unsigned int zone[15][15];    /**< 各区域信息*/
} isp_zone;

/**
* AE统计值参数
*/
typedef struct isp_core_ae_sta_info{
    unsigned char ae_histhresh[4];    /**< AE统计直方图bin边界 [0 ~ 255]*/
    unsigned short ae_hist[5];    /**< AE统计直方图bin值 [0 ~ 65535]*/
    unsigned char ae_stat_nodeh;    /**< 水平方向有效统计区域个数 [0 ~ 15]*/
    unsigned char ae_stat_nodev;    /**< 垂直方向有效统计区域个数 [0 ~ 15]*/
} isp_ae_hist;

/**
 * 白平衡模式
 */
enum isp_core_wb_mode {
    ISP_CORE_WB_MODE_AUTO = 0,            /**< 自动模式 */
    ISP_CORE_WB_MODE_MANUAL,            /**< 手动模式 */
    ISP_CORE_WB_MODE_DAY_LIGHT,            /**< 晴天 */
    ISP_CORE_WB_MODE_CLOUDY,            /**< 阴天 */
    ISP_CORE_WB_MODE_INCANDESCENT,        /**< 白炽灯 */
    ISP_CORE_WB_MODE_FLOURESCENT,        /**< 荧光灯 */
    ISP_CORE_WB_MODE_TWILIGHT,            /**< 黄昏 */
    ISP_CORE_WB_MODE_SHADE,                /**< 阴影 */
    ISP_CORE_WB_MODE_WARM_FLOURESCENT,    /**< 暖色荧光灯 */
    ISP_CORE_WB_MODE_CUSTOM,    /**< 自定义模式 */
};

/**
 * 白平衡参数
 */
typedef struct isp_core_wb_attr{
    enum isp_core_wb_mode mode;        /**< 白平衡模式，分为自动与手动模式 */
    uint16_t rgain;            /**< 红色增益，手动模式时有效 */
    uint16_t bgain;            /**< 蓝色增益，手动模式时有效 */
} isp_wb;

/**
 * gamma
 */
typedef struct isp_core_gamma_attr{
    unsigned short gamma[129];        /**< gamma参数数组，有129个点 */
} isp_gamma;



isp_tuning_hd_t *soc_isp_tuning_detect(int index);

int soc_isp_module_g_attr(isp_tuning_hd_t *hd, isp_module_ctrl *module);

int soc_isp_module_s_attr(isp_tuning_hd_t *hd, isp_module_ctrl *module);

int soc_isp_day_or_night_g_ctrl(isp_tuning_hd_t *hd, isp_running_mode *mode);

int soc_isp_day_or_night_s_ctrl(isp_tuning_hd_t *hd, isp_running_mode mode);

int soc_isp_hflip_g_control(isp_tuning_hd_t *hd, isp_tuning_ops_mode *mode);

int soc_isp_hflip_s_control(isp_tuning_hd_t *hd, isp_tuning_ops_mode mode);

int soc_isp_vflip_g_control(isp_tuning_hd_t *hd, isp_tuning_ops_mode *mode);

int soc_isp_vflip_s_control(isp_tuning_hd_t *hd, isp_tuning_ops_mode mode);

int soc_isp_fps_g_control(isp_tuning_hd_t *hd, uint32_t *fps_num, uint32_t *fps_den);

int soc_isp_fps_s_control(isp_tuning_hd_t *hd, uint32_t fps_num, uint32_t fps_den);

int soc_isp_brightness_g_ctrl(isp_tuning_hd_t *hd, unsigned char *brightness);

int soc_isp_brightness_s_ctrl(isp_tuning_hd_t *hd, unsigned char brightness);

int soc_isp_contrast_g_ctrl(isp_tuning_hd_t *hd, unsigned char *contrast);

int soc_isp_contrast_s_ctrl(isp_tuning_hd_t *hd, unsigned char contrast);

int soc_isp_saturation_g_ctrl(isp_tuning_hd_t *hd, unsigned char *saturation);

int soc_isp_saturation_s_ctrl(isp_tuning_hd_t *hd, unsigned char saturation);

int soc_isp_sharpness_g_ctrl(isp_tuning_hd_t *hd, unsigned char *sharpness);

int soc_isp_sharpness_s_ctrl(isp_tuning_hd_t *hd, unsigned char sharpness);

int soc_isp_flicker_g_ctrl(isp_tuning_hd_t *hd, isp_anti_flicker_attr *attr);

int soc_isp_flicker_s_ctrl(isp_tuning_hd_t *hd, isp_anti_flicker_attr attr);

int soc_isp_ev_g_attr(isp_tuning_hd_t *hd, isp_ev_attr *attr);

int soc_isp_expr_g_ctrl(isp_tuning_hd_t *hd, isp_expr *expr);

int soc_isp_expr_s_ctrl(isp_tuning_hd_t *hd, isp_expr *expr);

int soc_isp_max_again_g_ctrl(isp_tuning_hd_t *hd, uint32_t *gain);

int soc_isp_max_again_s_ctrl(isp_tuning_hd_t *hd, uint32_t gain);

int soc_isp_max_dgain_g_ctrl(isp_tuning_hd_t *hd, uint32_t *gain);

int soc_isp_max_dgain_s_ctrl(isp_tuning_hd_t *hd, uint32_t gain);

int soc_isp_tgain_g_ctrl(isp_tuning_hd_t *hd, uint32_t *gain);

int soc_isp_ae_min_g_attr(isp_tuning_hd_t *hd, isp_ae_min *ae_min);

int soc_isp_ae_min_s_attr(isp_tuning_hd_t *hd, isp_ae_min *ae_min);

int soc_isp_ev_start_s_ctrl(isp_tuning_hd_t *hd, unsigned int ev_start);

int soc_isp_ae_luma_g_ctrl(isp_tuning_hd_t *hd, unsigned char *luma);

int soc_isp_hi_light_depress_g_ctrl(isp_tuning_hd_t *hd, unsigned int *strength);

int soc_isp_hi_light_depress_s_ctrl(isp_tuning_hd_t *hd, unsigned int strength);

int soc_isp_ae_zone_weight_g_attr(isp_tuning_hd_t *hd, isp_weight *ae_weight);

int soc_isp_ae_zone_weight_s_attr(isp_tuning_hd_t *hd, isp_weight *ae_weight);

int soc_isp_ae_g_roi(isp_tuning_hd_t *hd, isp_weight *roi_weight);

int soc_isp_ae_s_roi(isp_tuning_hd_t *hd, isp_weight *roi_weight);

int soc_isp_ae_zone_g_ctrl(isp_tuning_hd_t *hd, isp_zone *ae_zone);

int soc_isp_ae_hist_g_attr(isp_tuning_hd_t *hd, isp_ae_hist *ae_hist);

int soc_isp_ae_hist_s_attr(isp_tuning_hd_t *hd, isp_ae_hist *ae_hist);

int soc_isp_wb_g_ctrl(isp_tuning_hd_t *hd, isp_wb *wb);

int soc_isp_wb_s_ctrl(isp_tuning_hd_t *hd, isp_wb *wb);

int soc_isp_wb_statis_g_ctrl(isp_tuning_hd_t *hd, isp_wb *wb);

int soc_isp_wb_statis_global_g_ctrl(isp_tuning_hd_t *hd, isp_wb *wb);

int soc_isp_gamma_g_attr(isp_tuning_hd_t *hd, isp_gamma *gamma);

int soc_isp_gamma_s_attr(isp_tuning_hd_t *hd, isp_gamma *gamma);

int soc_isp_adr_strength_g_ctrl(isp_tuning_hd_t *hd, uint32_t *strength);

int soc_isp_adr_strength_s_ctrl(isp_tuning_hd_t *hd, uint32_t strength);


#endif //__ISP_TUNING_H__
