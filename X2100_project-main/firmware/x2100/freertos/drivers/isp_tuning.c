/*
 * Copyright (C) 2020 Ingenic Semiconductor Co., Ltd.
 *
 */

#include <driver/isp_tuning.h>



isp_tuning_hd_t *isp_tuning_detect(int index)
{
    return soc_isp_tuning_detect(index);
}

int isp_tuning_get_module_control(isp_tuning_hd_t *hd, isp_module_ctrl *isp_module)
{
    return soc_isp_module_g_attr(hd, isp_module);
}

int isp_tuning_set_module_control(isp_tuning_hd_t *hd, isp_module_ctrl *isp_module)
{
    return soc_isp_module_s_attr(hd, isp_module);
}

int isp_tuning_get_isp_running_mode(isp_tuning_hd_t *hd, isp_running_mode *mode)
{
    return soc_isp_day_or_night_g_ctrl(hd, mode);
}

int isp_tuning_set_isp_running_mode(isp_tuning_hd_t *hd, isp_running_mode mode)
{
    return soc_isp_day_or_night_s_ctrl(hd, mode);
}

int isp_tuning_get_isp_hflip(isp_tuning_hd_t *hd, isp_tuning_ops_mode *mode)
{
    return soc_isp_hflip_g_control(hd, mode);
}

int isp_tuning_set_isp_hflip(isp_tuning_hd_t *hd, isp_tuning_ops_mode mode)
{
    return soc_isp_hflip_s_control(hd, mode);
}

int isp_tuning_get_isp_vflip(isp_tuning_hd_t *hd, isp_tuning_ops_mode *mode)
{
    return soc_isp_vflip_g_control(hd, mode);
}

int isp_tuning_set_isp_vflip(isp_tuning_hd_t *hd, isp_tuning_ops_mode mode)
{
    return soc_isp_vflip_s_control(hd, mode);
}

int isp_tuning_get_sensor_fps(isp_tuning_hd_t *hd, uint32_t *fps_num, uint32_t *fps_den)
{
    return soc_isp_fps_g_control(hd, fps_num, fps_den);
}

int isp_tuning_set_sensor_fps(isp_tuning_hd_t *hd, uint32_t fps_num, uint32_t fps_den)
{
    return soc_isp_fps_s_control(hd, fps_num, fps_den);
}

int isp_tuning_get_brightness(isp_tuning_hd_t *hd, unsigned char *brightness)
{
    return soc_isp_brightness_g_ctrl(hd, brightness);
}

int isp_tuning_set_brightness(isp_tuning_hd_t *hd, unsigned char brightness)
{
    return soc_isp_brightness_s_ctrl(hd, brightness);
}

int isp_tuning_get_contrast(isp_tuning_hd_t *hd, unsigned char *contrast)
{
    return soc_isp_contrast_g_ctrl(hd, contrast);
}

int isp_tuning_set_contrast(isp_tuning_hd_t *hd, unsigned char contrast)
{
    return soc_isp_contrast_s_ctrl(hd, contrast);
}

int isp_tuning_get_saturation(isp_tuning_hd_t *hd, unsigned char *saturation)
{
    return soc_isp_saturation_g_ctrl(hd, saturation);
}

int isp_tuning_set_saturation(isp_tuning_hd_t *hd, unsigned char saturation)
{
    return soc_isp_saturation_s_ctrl(hd, saturation);
}

int isp_tuning_get_sharpness(isp_tuning_hd_t *hd, unsigned char *sharpness)
{
    return soc_isp_sharpness_g_ctrl(hd, sharpness);
}

int isp_tuning_set_sharpness(isp_tuning_hd_t *hd, unsigned char sharpness)
{
    return soc_isp_sharpness_s_ctrl(hd, sharpness);
}

int isp_tuning_get_anti_flicker_attr(isp_tuning_hd_t *hd, isp_anti_flicker_attr *attr)
{
    return soc_isp_flicker_g_ctrl(hd, attr);
}

int isp_tuning_set_anti_flicker_attr(isp_tuning_hd_t *hd, isp_anti_flicker_attr attr)
{
    return soc_isp_flicker_s_ctrl(hd, attr);
}

int isp_tuning_get_ev_attr(isp_tuning_hd_t *hd, isp_ev_attr *attr)
{
    return soc_isp_ev_g_attr(hd, attr);
}

int isp_tuning_get_expr(isp_tuning_hd_t *hd, isp_expr *expr)
{
    return soc_isp_expr_g_ctrl(hd, expr);
}

int isp_tuning_set_expr(isp_tuning_hd_t *hd, isp_expr *expr)
{
    return soc_isp_expr_s_ctrl(hd, expr);
}

int isp_tuning_get_max_again(isp_tuning_hd_t *hd, uint32_t *gain)
{
    return soc_isp_max_again_g_ctrl(hd, gain);
}

int isp_tuning_set_max_again(isp_tuning_hd_t *hd, uint32_t gain)
{
    return soc_isp_max_again_s_ctrl(hd, gain);
}

int isp_tuning_get_max_dgain(isp_tuning_hd_t *hd, uint32_t *gain)
{
    return soc_isp_max_dgain_g_ctrl(hd, gain);
}

int isp_tuning_set_max_dgain(isp_tuning_hd_t *hd, uint32_t gain)
{
    return soc_isp_max_dgain_s_ctrl(hd, gain);
}

int isp_tuning_get_total_gain(isp_tuning_hd_t *hd, uint32_t *gain)
{
    return soc_isp_tgain_g_ctrl(hd, gain);
}

int isp_tuning_get_ae_min(isp_tuning_hd_t *hd, isp_ae_min *ae_min)
{
    return soc_isp_ae_min_g_attr(hd, ae_min);
}

int isp_tuning_set_ae_min(isp_tuning_hd_t *hd, isp_ae_min *ae_min)
{
    return soc_isp_ae_min_s_attr(hd, ae_min);
}

int isp_tuning_get_ae_luma(isp_tuning_hd_t *hd, unsigned char *luma)
{
    return soc_isp_ae_luma_g_ctrl(hd, luma);
}

int isp_tuning_get_hi_light_depress(isp_tuning_hd_t *hd, uint32_t *strength)
{
    return soc_isp_hi_light_depress_g_ctrl(hd, strength);
}

int isp_tuning_set_hi_light_depress(isp_tuning_hd_t *hd, uint32_t strength)
{
    return soc_isp_hi_light_depress_s_ctrl(hd, strength);
}

int isp_tuning_get_ae_zone(isp_tuning_hd_t *hd, isp_zone *ae_zone)
{
    return soc_isp_ae_zone_g_ctrl(hd, ae_zone);
}

int isp_tuning_get_ae_hist(isp_tuning_hd_t *hd, isp_ae_hist *ae_hist)
{
    return soc_isp_ae_hist_g_attr(hd, ae_hist);
}

int isp_tuning_set_ae_hist(isp_tuning_hd_t *hd, isp_ae_hist *ae_hist)
{
    return soc_isp_ae_hist_s_attr(hd, ae_hist);
}

int isp_tuning_get_ae_roi(isp_tuning_hd_t *hd, isp_weight *roi_weight)
{
    return soc_isp_ae_g_roi(hd, roi_weight);
}

int isp_tuning_set_ae_roi(isp_tuning_hd_t *hd, isp_weight *roi_weight)
{
    return soc_isp_ae_s_roi(hd, roi_weight);
}

int isp_tuning_get_ae_weight(isp_tuning_hd_t *hd, isp_weight *ae_weight)
{
    return soc_isp_ae_zone_weight_g_attr(hd, ae_weight);
}

int isp_tuning_set_ae_weight(isp_tuning_hd_t *hd, isp_weight *ae_weight)
{
    return soc_isp_ae_zone_weight_s_attr(hd, ae_weight);
}

int isp_tuning_get_wb(isp_tuning_hd_t *hd, isp_wb *wb)
{
    return soc_isp_wb_g_ctrl(hd, wb);
}

int isp_tuning_set_wb(isp_tuning_hd_t *hd, isp_wb *wb)
{
    return soc_isp_wb_s_ctrl(hd, wb);
}

int isp_tuning_get_wb_statis(isp_tuning_hd_t *hd, isp_wb *wb)
{
    return soc_isp_wb_statis_g_ctrl(hd, wb);
}

int isp_tuning_get_wb_gol_statis(isp_tuning_hd_t *hd, isp_wb *wb)
{
    return soc_isp_wb_statis_global_g_ctrl(hd, wb);
}

int isp_tuning_get_gamma(isp_tuning_hd_t *hd, isp_gamma *gamma)
{
    return soc_isp_gamma_g_attr(hd, gamma);
}

int isp_tuning_set_gamma(isp_tuning_hd_t *hd, isp_gamma *gamma)
{
    return soc_isp_gamma_s_attr(hd, gamma);
}

int isp_tuning_get_adr_strength(isp_tuning_hd_t *hd, uint32_t *strength)
{
    return soc_isp_adr_strength_g_ctrl(hd, strength);
}

int isp_tuning_set_adr_strength(isp_tuning_hd_t *hd, uint32_t strength)
{
    return soc_isp_adr_strength_s_ctrl(hd, strength);
}
