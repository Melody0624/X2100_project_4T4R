#include <malloc.h>
#include <errno.h>
#include "isp.h"
#include "isp-core/inc/tiziano_core_tuning.h"
#include <soc/isp_tuning.h>



#define hd_to_isp_tuning(hd) container_of(hd, struct isp_core_tuning_driver, hd)

/*
 * the module must be actived firstly.
 */
#define check_isp_tuning_state(tuning)                          \
do {                                                            \
    if(tuning->state < STATE_OPEN){                             \
        printf("isp tuning fail, please stream on first\n");    \
        mutex_unlock(&tuning->mlock);                           \
        return -EPERM;                                          \
    }                                                           \
} while(0)




static struct isp_core_tuning_driver tuning_dev[] = {
    {
        .index          = 0,
        .device_name    = "isp-tuning0",
        .state          = STATE_CLOSE,
    },
    {
        .index          = 1,
        .device_name    = "isp-tuning1",
        .state          = STATE_CLOSE,
    }
};



isp_tuning_hd_t *soc_isp_tuning_detect(int index)
{
    assert(index < 2);
    struct isp_core_tuning_driver *tuning = &tuning_dev[index];

    return &tuning->hd;
}

int soc_isp_module_g_attr(isp_tuning_hd_t *hd, isp_module_ctrl *module)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    if (NULL == module) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_g_module_control(tuning->core_tuning, (tisp_module_control_t *)module);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_module_s_attr(isp_tuning_hd_t *hd, isp_module_ctrl *module)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_module_control_t *tisp_module;

    if (NULL == module) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }
    tisp_module = (tisp_module_control_t *)module;

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_s_module_control(tuning->core_tuning, *tisp_module);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_day_or_night_g_ctrl(isp_tuning_hd_t *hd, isp_running_mode *mode)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    if (NULL == mode) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    *mode = (isp_running_mode)tisp_day_or_night_g_ctrl(tuning->core_tuning);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_day_or_night_s_ctrl(isp_tuning_hd_t *hd, isp_running_mode mode)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    struct jz_isp_data *isp = tuning->parent;
    tisp_core_tuning_t *core_tuning = tuning->core_tuning;

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    if(core_tuning->day_night != mode){
        isp->dn_state = mode;
        isp->daynight_change = 1;
    }

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_hflip_g_control(isp_tuning_hd_t *hd, isp_tuning_ops_mode *mode)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    if (NULL == mode) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    *mode = tuning->core_tuning->hflip;

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_hflip_s_control(isp_tuning_hd_t *hd, isp_tuning_ops_mode mode)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    struct jz_isp_data *isp = tuning->parent;
    tisp_core_tuning_t *core_tuning = tuning->core_tuning;

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    if(core_tuning->hflip != mode){
        isp->hflip_state = mode;
        isp->hflip_change = 1;
    }

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_vflip_g_control(isp_tuning_hd_t *hd, isp_tuning_ops_mode *mode)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    if (NULL == mode) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    *mode = tuning->core_tuning->vflip;

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_vflip_s_control(isp_tuning_hd_t *hd, isp_tuning_ops_mode mode)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    struct jz_isp_data *isp = tuning->parent;
    tisp_core_tuning_t *core_tuning = tuning->core_tuning;

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    if(core_tuning->vflip != mode){
        isp->vflip_state = mode;
        isp->vflip_change = 1;
    }

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_fps_g_control(isp_tuning_hd_t *hd, uint32_t *fps_num, uint32_t *fps_den)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    struct jz_isp_data *isp = tuning->parent;
    struct sensor_attr *attr = isp->camera.sensor;

    if (NULL == fps_num || NULL == fps_den) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    *fps_num = (attr->sensor_info.fps >> 16) & 0xffff;
    *fps_den = attr->sensor_info.fps & 0xffff;

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_fps_s_control(isp_tuning_hd_t *hd, uint32_t fps_num, uint32_t fps_den)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    struct jz_isp_data *isp = tuning->parent;
    struct sensor_attr *attr = isp->camera.sensor;
    unsigned int fps = (fps_num << 16) | fps_den;
    int ret = 0;

    if(attr->ops.set_fps == NULL){
        printf("attr->ops.set_fps is NULL\n");
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    if(fps != attr->sensor_info.fps){
        ret = attr->ops.set_fps(fps);
        if (ret != 0) {
            printf("Failed to set sensor fps=%u, ret=%d\n", fps, ret);
            goto done;
        }
        tisp_set_fps(tuning->core_tuning, fps);
    }

done:
    mutex_unlock(&tuning->mlock);
    return ret;
}

int soc_isp_brightness_g_ctrl(isp_tuning_hd_t *hd, unsigned char *brightness)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    if (NULL == brightness) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    *brightness = tisp_get_brightness(tuning->core_tuning);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_brightness_s_ctrl(isp_tuning_hd_t *hd, unsigned char brightness)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_set_brightness(tuning->core_tuning, brightness);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_contrast_g_ctrl(isp_tuning_hd_t *hd, unsigned char *contrast)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    if (NULL == contrast) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    *contrast = tisp_get_contrast(tuning->core_tuning);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_contrast_s_ctrl(isp_tuning_hd_t *hd, unsigned char contrast)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_set_contrast(tuning->core_tuning, contrast);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_saturation_g_ctrl(isp_tuning_hd_t *hd, unsigned char *saturation)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    if (NULL == saturation) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    *saturation = tisp_get_saturation(tuning->core_tuning);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_saturation_s_ctrl(isp_tuning_hd_t *hd, unsigned char saturation)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_set_saturation(tuning->core_tuning, saturation);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_sharpness_g_ctrl(isp_tuning_hd_t *hd, unsigned char *sharpness)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    if (NULL == sharpness) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    *sharpness = tisp_get_sharpness(tuning->core_tuning);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_sharpness_s_ctrl(isp_tuning_hd_t *hd, unsigned char sharpness)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_set_sharpness(tuning->core_tuning, sharpness);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_flicker_g_ctrl(isp_tuning_hd_t *hd, isp_anti_flicker_attr *attr)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_core_tuning_t *core_tuning = tuning->core_tuning;

    if (NULL == attr) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    *attr = core_tuning->flicker_hz;

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_flicker_s_ctrl(isp_tuning_hd_t *hd, isp_anti_flicker_attr attr)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    int ret = 0;

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    switch(attr){
        case ISP_ANTIFLICKER_DISABLE:
        case ISP_ANTIFLICKER_50HZ:
        case ISP_ANTIFLICKER_60HZ:
            break;
        default:
            printf("%s: Can not support this val:%d\n", __func__, attr);
            ret = -EINVAL;
            goto done;
    }

    tisp_s_antiflick(tuning->core_tuning, attr);

done:
    mutex_unlock(&tuning->mlock);
    return ret;
}

int soc_isp_ev_g_attr(isp_tuning_hd_t *hd, isp_ev_attr *attr)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_ev_attr_t tev_attr;

    if (NULL == attr) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_g_ev_attr(tuning->core_tuning, &tev_attr);

    attr->ev = tev_attr.ev;
    attr->again = tev_attr.again;
    attr->dgain = tev_attr.dgain;
    attr->gain_log2 = tev_attr.gain_log2;
    attr->expr_us = tev_attr.expr_us;
    attr->ev_log2 = tev_attr.ev_log2;

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_expr_g_ctrl(isp_tuning_hd_t *hd, isp_expr *expr)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    struct jz_isp_data *isp = tuning->parent;
    struct sensor_attr *attr = isp->camera.sensor;
    tisp_ev_attr_t ae_attr;

    if (NULL == expr) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_g_ae_attr(tuning->core_tuning, &ae_attr);

    if (ae_attr.manual_it == 0)
        expr->g_attr.mode = ISP_CORE_EXPR_MODE_AUTO;
    else if (ae_attr.manual_it == 1)
        expr->g_attr.mode = ISP_CORE_EXPR_MODE_MANUAL;
    expr->g_attr.integration_time = ae_attr.integration_time;
    expr->g_attr.integration_time_min = attr->sensor_info.min_integration_time;
    expr->g_attr.integration_time_max = attr->sensor_info.max_integration_time;
    expr->g_attr.one_line_expr_in_us = attr->sensor_info.one_line_expr_in_us;

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_expr_s_ctrl(isp_tuning_hd_t *hd, isp_expr *expr)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    struct jz_isp_data *isp = tuning->parent;
    struct sensor_attr *attr = isp->camera.sensor;
    tisp_ev_attr_t ae_attr;
    int ret = 0;

    if (NULL == expr) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    if (expr->s_attr.mode == ISP_CORE_EXPR_MODE_AUTO) {
        ae_attr.manual_it = 0;
    } else if (expr->s_attr.mode == ISP_CORE_EXPR_MODE_MANUAL) {
        ae_attr.manual_it = 1;
        if ((expr->s_attr.unit == ISP_CORE_EXPR_UNIT_US)) {
            if (attr->sensor_info.one_line_expr_in_us != 0)
                ae_attr.integration_time = expr->s_attr.time/attr->sensor_info.one_line_expr_in_us;
            else {
                printf("err: %s, one_line_expr_in_us = %d \n", __func__, attr->sensor_info.one_line_expr_in_us);
                ret = -EINVAL;
                goto done;
            }
        } else if((expr->s_attr.unit == ISP_CORE_EXPR_UNIT_LINE)) {
            ae_attr.integration_time = expr->s_attr.time;
        } else {
            printf("Err:%s, can not support this unit\n", __func__);
            ret = -EINVAL;
            goto done;
        }

    } else {
        printf("Err:%s, can not support this mode\n", __func__);
        ret = -EINVAL;
        goto done;
    }

    tisp_s_ae_attr(tuning->core_tuning, ae_attr);

done:
    mutex_unlock(&tuning->mlock);
    return ret;
}

int soc_isp_max_again_g_ctrl(isp_tuning_hd_t *hd, uint32_t *gain)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_ev_attr_t ev_attr;

    if (NULL == gain) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_g_ev_attr(tuning->core_tuning, &ev_attr);

    *gain = ev_attr.max_again;

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_max_again_s_ctrl(isp_tuning_hd_t *hd, uint32_t gain)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    unsigned int max_again = gain;

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_s_max_again(tuning->core_tuning, max_again);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_max_dgain_g_ctrl(isp_tuning_hd_t *hd, uint32_t *gain)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_ev_attr_t ev_attr;

    if (NULL == gain) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_g_ev_attr(tuning->core_tuning, &ev_attr);

    *gain = ev_attr.max_isp_dgain;

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_max_dgain_s_ctrl(isp_tuning_hd_t *hd, uint32_t gain)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    unsigned int max_isp_dgain = gain;

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_s_max_isp_dgain(tuning->core_tuning, max_isp_dgain);

    mutex_unlock(&tuning->mlock);

    return 0;
}

/* the format of return value is 8.8 */
int soc_isp_tgain_g_ctrl(isp_tuning_hd_t *hd, uint32_t *gain)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_ev_attr_t ev_attr;

    if (NULL == gain) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_g_ev_attr(tuning->core_tuning, &ev_attr);

    *gain = ev_attr.total_gain;

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_ae_min_g_attr(isp_tuning_hd_t *hd, isp_ae_min *ae_min)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    if (NULL == ae_min) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_g_ae_min(tuning->core_tuning, (tisp_ae_ex_min_t *)ae_min);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_ae_min_s_attr(isp_tuning_hd_t *hd, isp_ae_min *ae_min)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_ae_ex_min_t *tisp_ae_min;
    int ret;

    if (NULL == ae_min) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }
    tisp_ae_min = (tisp_ae_ex_min_t *)ae_min;

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    ret = tisp_s_ae_min(tuning->core_tuning, *tisp_ae_min);

    mutex_unlock(&tuning->mlock);

    return ret;
}

int soc_isp_ev_start_s_ctrl(isp_tuning_hd_t *hd, unsigned int ev_start)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);
    
    tisp_s_ev_start(tuning->core_tuning, ev_start);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_ae_luma_g_ctrl(isp_tuning_hd_t *hd, unsigned char *luma)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    if (NULL == luma) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_g_ae_luma(tuning->core_tuning, luma);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_hi_light_depress_g_ctrl(isp_tuning_hd_t *hd, unsigned int *strength)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    int ret;

    if (NULL == strength) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    ret = tisp_g_Hilightdepress(tuning->core_tuning, strength);

    mutex_unlock(&tuning->mlock);

    return ret;
}

int soc_isp_hi_light_depress_s_ctrl(isp_tuning_hd_t *hd, unsigned int strength)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    int ret;

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    ret = tisp_s_Hilightdepress(tuning->core_tuning, strength);

    mutex_unlock(&tuning->mlock);

    return ret;
}

int soc_isp_ae_zone_weight_g_attr(isp_tuning_hd_t *hd, isp_weight *ae_weight)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_3a_weight_t zone_weight;
    int cols,rows;
    int ret = 0;

    if (NULL == ae_weight) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    ret = tisp_g_aezone_weight(tuning->core_tuning, &zone_weight);
    if(ret != 0){
        printf("tisp_g_aezone_weight failed!\n");
        goto done;
    }

    for(cols = 0;cols < 15;cols++){
        for(rows = 0;rows < 15;rows++){
            ae_weight->weight[cols][rows] = (unsigned char)(zone_weight.weight[rows+cols*15] & 0xff);
        }
    }

done:
    mutex_unlock(&tuning->mlock);
    return ret;
}

int soc_isp_ae_zone_weight_s_attr(isp_tuning_hd_t *hd, isp_weight *ae_weight)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_3a_weight_t zone_weight;
    int cols,rows;
    int ret = 0;

    if (NULL == ae_weight) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    for(cols = 0;cols < 15;cols++){
        for(rows = 0;rows < 15;rows++){
            if(ae_weight->weight[cols][rows] > 8){
                printf("%s:ae zone weight overflow!\n", __func__);
                ret = -EINVAL;
                goto done;
            }
            zone_weight.weight[rows+cols*15] = (unsigned int)ae_weight->weight[cols][rows];
        }
    }

    ret = tisp_s_aezone_weight(tuning->core_tuning, &zone_weight);

done:
    mutex_unlock(&tuning->mlock);
    return ret;
}

int soc_isp_ae_g_roi(isp_tuning_hd_t *hd, isp_weight *roi_weight)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_3a_weight_t ae_roi_weight;
    int cols,rows;
    int ret = 0;

    if (NULL == roi_weight) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    ret = tisp_g_aeroi_weight(tuning->core_tuning, &ae_roi_weight);
    if(ret != 0){
        printf("tisp_g_aeroi_weight failed!\n");
        goto done;
    }

    for(cols = 0;cols < 15;cols++){
        for(rows = 0;rows < 15;rows++){
            roi_weight->weight[cols][rows] = (unsigned char)(ae_roi_weight.weight[rows+cols*15] & 0xff);
        }
    }

done:
    mutex_unlock(&tuning->mlock);
    return ret;
}

int soc_isp_ae_s_roi(isp_tuning_hd_t *hd, isp_weight *roi_weight)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_3a_weight_t tisp_roi_weight;
    int cols,rows;
    int ret = 0;

    if (NULL == roi_weight) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    for(cols = 0;cols < 15;cols++){
        for(rows = 0;rows < 15;rows++){
            if(roi_weight->weight[cols][rows] > 8){
                printf("%s:ae weight overflow!\n", __func__);
                ret = -EINVAL;
                goto done;
            }
            tisp_roi_weight.weight[rows+cols*15] = (unsigned int)roi_weight->weight[cols][rows];
        }
    }

    ret = tisp_s_aeroi_weight(tuning->core_tuning, &tisp_roi_weight);
    if(ret != 0)
        printf("tisp_s_aeroi_weight failed!\n");

done:
    mutex_unlock(&tuning->mlock);
    return ret;
}

int soc_isp_ae_zone_g_ctrl(isp_tuning_hd_t *hd, isp_zone *ae_zone)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);

    if (NULL == ae_zone) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_g_ae_zone(tuning->core_tuning, (tisp_zone_info_t *)ae_zone);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_ae_hist_g_attr(isp_tuning_hd_t *hd, isp_ae_hist *ae_hist)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_ae_sta_t ae_sta;

    if (NULL == ae_hist) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_g_ae_hist(tuning->core_tuning, &ae_sta);

    ae_hist->ae_histhresh[0] = ae_sta.ae_hist_nodes[0];
    ae_hist->ae_histhresh[1] = ae_sta.ae_hist_nodes[1];
    ae_hist->ae_histhresh[2] = ae_sta.ae_hist_nodes[2];
    ae_hist->ae_histhresh[3] = ae_sta.ae_hist_nodes[3];

    ae_hist->ae_hist[0] = ae_sta.ae_hist_5bin[0];
    ae_hist->ae_hist[1] = ae_sta.ae_hist_5bin[1];
    ae_hist->ae_hist[2] = ae_sta.ae_hist_5bin[2];
    ae_hist->ae_hist[3] = ae_sta.ae_hist_5bin[3];
    ae_hist->ae_hist[4] = ae_sta.ae_hist_5bin[4];

    ae_hist->ae_stat_nodeh = ae_sta.ae_hist_hv[0];
    ae_hist->ae_stat_nodev = ae_sta.ae_hist_hv[1];

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_ae_hist_s_attr(isp_tuning_hd_t *hd, isp_ae_hist *ae_hist)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_ae_sta_t ae_sta;

    if (NULL == ae_hist) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    ae_sta.ae_hist_nodes[0] = ae_hist->ae_histhresh[0];
    ae_sta.ae_hist_nodes[1] = ae_hist->ae_histhresh[1];
    ae_sta.ae_hist_nodes[2] = ae_hist->ae_histhresh[2];
    ae_sta.ae_hist_nodes[3] = ae_hist->ae_histhresh[3];

    tisp_s_ae_hist(tuning->core_tuning, ae_sta);

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_wb_g_ctrl(isp_tuning_hd_t *hd, isp_wb *wb)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_wb_attr_t twb_attr;
    int ret = 0;

    if (NULL == wb) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    ret = tisp_g_wb_attr(tuning->core_tuning, &twb_attr);
    if(ret != 0){
        printf("tisp_g_wb_attr failed!\n");
        goto done;
    }

    wb->mode  = twb_attr.tisp_wb_manual;
    wb->rgain = twb_attr.tisp_wb_rg;
    wb->bgain = twb_attr.tisp_wb_bg;

done:
    mutex_unlock(&tuning->mlock);
    return ret;
}

int soc_isp_wb_s_ctrl(isp_tuning_hd_t *hd, isp_wb *wb)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_wb_attr_t tisp_wb_attr;
    int ret = 0;

    if (NULL == wb) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    if (wb->mode < ISP_CORE_WB_MODE_AUTO || wb->mode > ISP_CORE_WB_MODE_CUSTOM) {
        printf("%s, error mode %d", __func__, wb->mode);
        goto done;
    }

    tisp_wb_attr.tisp_wb_manual = wb->mode;
    tisp_wb_attr.tisp_wb_rg = wb->rgain;
    tisp_wb_attr.tisp_wb_bg = wb->bgain;
    tisp_s_wb_attr(tuning->core_tuning, tisp_wb_attr);

done:
    mutex_unlock(&tuning->mlock);
    return ret;
}

int soc_isp_wb_statis_g_ctrl(isp_tuning_hd_t *hd, isp_wb *wb)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_wb_attr_t wb_attr;

    if (NULL == wb) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_g_wb_attr(tuning->core_tuning, &wb_attr);
    wb->rgain = wb_attr.tisp_wb_rg_sta_weight & 0xffff;
    wb->bgain = wb_attr.tisp_wb_bg_sta_weight & 0xffff;

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_wb_statis_global_g_ctrl(isp_tuning_hd_t *hd, isp_wb *wb)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_wb_attr_t wb_attr;

    if (NULL == wb) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    tisp_g_wb_attr(tuning->core_tuning, &wb_attr);
    wb->rgain = wb_attr.tisp_wb_rg_sta_global & 0xffff;
    wb->bgain = wb_attr.tisp_wb_bg_sta_global & 0xffff;

    mutex_unlock(&tuning->mlock);

    return 0;
}

int soc_isp_gamma_g_attr(isp_tuning_hd_t *hd, isp_gamma *gamma)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_gamma_lut_t tgamma;
    int nodes_num = 0;
    int ret = 0;

    if (NULL == gamma) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    ret = tisp_g_Gamma(tuning->core_tuning, &tgamma);
    if(ret != 0){
        printf("tisp_g_Gamma failed!\n");
        goto done;
    }
    for(nodes_num = 0; nodes_num < 129; nodes_num++)
        gamma->gamma[nodes_num] = (unsigned short)(tgamma.gamma[nodes_num] & 0xffff);

done:
    mutex_unlock(&tuning->mlock);
    return ret;
}

int soc_isp_gamma_s_attr(isp_tuning_hd_t *hd, isp_gamma *gamma)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    tisp_gamma_lut_t tgamma;
    int nodes_num = 0;
    int ret;

    if (NULL == gamma) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    for(nodes_num = 0; nodes_num < 129;nodes_num++)
        tgamma.gamma[nodes_num] = (unsigned int)gamma->gamma[nodes_num];

    ret = tisp_s_Gamma(tuning->core_tuning, &tgamma);

    mutex_unlock(&tuning->mlock);

    return ret;
}

int soc_isp_adr_strength_g_ctrl(isp_tuning_hd_t *hd, uint32_t *strength)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    int ret;

    if (NULL == strength) {
        printf("%s, param is NULL\n", __func__);
        return -EINVAL;
    }

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    ret = tisp_g_adr_strength(tuning->core_tuning, strength);

    mutex_unlock(&tuning->mlock);

    return ret;
}

int soc_isp_adr_strength_s_ctrl(isp_tuning_hd_t *hd, uint32_t strength)
{
    struct isp_core_tuning_driver *tuning = hd_to_isp_tuning(hd);
    int ret;

    mutex_lock(&tuning->mlock);

    check_isp_tuning_state(tuning);

    ret = tisp_s_adr_strength(tuning->core_tuning, strength);

    mutex_unlock(&tuning->mlock);

    return ret;
}



int tiziano_isp_tuning_activate(struct isp_core_tuning_driver *tuning)
{
    mutex_lock(&tuning->mlock);
    tuning->state = STATE_OPEN;
    mutex_unlock(&tuning->mlock);
    return 0;
}

int tiziano_isp_tuning_slake(struct isp_core_tuning_driver *tuning)
{
    mutex_lock(&tuning->mlock);
    tuning->state = STATE_CLOSE;
    mutex_unlock(&tuning->mlock);
    return 0;
}

int tiziano_isp_tuning_init(struct jz_isp_data *parent)
{
    tuning_dev[parent->index].parent = parent;
    tuning_dev[parent->index].core_tuning = &parent->core->core_tuning;
    tuning_dev[parent->index].hd.ptr = (void *)tuning_dev[parent->index].device_name;
    mutex_init(&tuning_dev[parent->index].mlock);

    parent->tuning = &tuning_dev[parent->index];

    return 0;
}

void tiziano_isp_tuning_deinit(struct jz_isp_data *parent)
{
    tuning_dev[parent->index].state = STATE_CLOSE;
    parent->tuning = NULL;
}
