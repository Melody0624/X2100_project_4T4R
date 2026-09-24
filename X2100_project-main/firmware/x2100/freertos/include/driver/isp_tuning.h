/*
 * isp tuning header file.
 *
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __ISPTuning_H__
#define __ISPTuning_H__

#include <stdint.h>
#include <soc/isp_tuning.h>


/**
 * 图像信号处理单元。主要包含图像效果设置、模式切换以及Sensor的注册添加删除等操作
 */


/**
 * @fn isp_tuning_hd_t *isp_tuning_detect(int index)
 *
 * isp tuning句柄
 *
 * @param[in] dev isp index
 *
 * @retval 非空 成功，返回tuning句柄
 * @retval 空 失败
 */
isp_tuning_hd_t *isp_tuning_detect(int index);

/**
 * @fn int isp_tuning_set_module_control(isp_tuning_hd_t *hd, isp_module_ctrl *isp_module)
 *
 * 设置ISP各个模块bypass功能
 *
 * @param[in] ispmodule ISP各个模块bypass功能.
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_set_module_control(isp_tuning_hd_t *hd, isp_module_ctrl *isp_module);

/**
 * @fn int isp_tuning_get_module_control(isp_tuning_hd_t *hd, isp_module_ctrl *isp_module)
 *
 * 获取ISP各个模块bypass功能.
 *
 * @param[out] ispmodule ISP各个模块bypass功能
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_module_control(isp_tuning_hd_t *hd, isp_module_ctrl *isp_module);

/**
 * @fn int isp_tuning_get_isp_running_mode(isp_tuning_hd_t *hd, isp_running_mode *mode)
 *
 * 获取ISP工作模式，正常模式或夜视模式。
 *
 * @param[in] mode操作参数指针
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_isp_running_mode(isp_tuning_hd_t *hd, isp_running_mode *mode);

/**
 * @fn int isp_tuning_set_isp_running_mode(isp_tuning_hd_t *hd, isp_running_mode mode)
 *
 * 设置ISP工作模式，正常模式或夜视模式；默认为正常模式。
 *
 * @param[in] mode运行模式参数
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_set_isp_running_mode(isp_tuning_hd_t *hd, isp_running_mode mode);

/**
 * @fn int isp_tuning_get_isp_hflip(isp_tuning_hd_t *hd, isp_tuning_ops_mode *mode)
 *
 * 获取ISP图像镜面效果功能的操作状态
 *
 * @param[in] pmode 操作参数指针
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_isp_hflip(isp_tuning_hd_t *hd, isp_tuning_ops_mode *mode);

/**
 * 设置ISP图像镜面效果功能是否使能
 *
 * @fn int isp_tuning_set_isp_hflip(isp_tuning_hd_t *hd, isp_tuning_ops_mode mode)
 *
 * @param[in] mode 是否使能镜面效果
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_set_isp_hflip(isp_tuning_hd_t *hd, isp_tuning_ops_mode mode);

/**
 * @fn int isp_tuning_get_isp_vflip(isp_tuning_hd_t *hd, isp_tuning_ops_mode *mode)
 *
 * 获取ISP图像上下反转效果功能的操作状态
 *
 * @param[in] pmode 操作参数指针
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_isp_vflip(isp_tuning_hd_t *hd, isp_tuning_ops_mode *mode);

/**
 * @fn int isp_tuning_set_isp_vflip(isp_tuning_hd_t *hd, isp_tuning_ops_mode mode)
 *
 * 设置ISP图像上下反转效果功能是否使能
 *
 * @param[in] mode 是否使能图像上下反转
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_set_isp_vflip(isp_tuning_hd_t *hd, isp_tuning_ops_mode mode);

/**
 * @fn int isp_tuning_get_sensor_fps(isp_tuning_hd_t *hd, uint32_t *fps_num, uint32_t *fps_den)
 *
 * 获取摄像头输出帧率
 *
 * @param[in] fps_num 获取帧率分子参数的指针
 * @param[in] fps_den 获取帧率分母参数的指针
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_sensor_fps(isp_tuning_hd_t *hd, uint32_t *fps_num, uint32_t *fps_den);

/**
 * @fn int isp_tuning_set_sensor_fps(isp_tuning_hd_t *hd, uint32_t fps_num, uint32_t fps_den)
 *
 * 设置摄像头输出帧率
 *
 * @param[in] fps_num 设定帧率的分子参数
 * @param[in] fps_den 设定帧率的分母参数
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_set_sensor_fps(isp_tuning_hd_t *hd, uint32_t fps_num, uint32_t fps_den);

/**
 * @fn int isp_tuning_get_brightness(isp_tuning_hd_t *hd, unsigned char *brightness)
 *
 * 获取ISP 综合效果图片亮度
 *
 * @param[in] bright 图片亮度参数指针
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @remark 默认值为128，大于128增加亮度，小于128降低亮度。
 *
 * @attention 在使用这个函数之前，必须保证ISP出图.
 */
int isp_tuning_get_brightness(isp_tuning_hd_t *hd, unsigned char *brightness);

/**
 * @fn int isp_tuning_set_brightness(isp_tuning_hd_t *hd, unsigned char brightness)
 *
 * 设置ISP 综合效果图片亮度
 *
 * @param[in] brightness 图片亮度参数
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @remark 默认值为128，大于128增加亮度，小于128降低亮度。
 *
 * @attention 在使用这个函数之前，必须保证ISP出图.
 */
int isp_tuning_set_brightness(isp_tuning_hd_t *hd, unsigned char brightness);

/**
 * @fn int isp_tuning_get_contrast(isp_tuning_hd_t *hd, unsigned char *contrast)
 *
 * 获取ISP 综合效果图片对比度
 *
 * @param[in] contrast 图片对比度参数指针
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @remark 默认值为128，大于128增加对比度，小于128降低对比度。
 *
 * @attention 在使用这个函数之前，必须保证ISP出图.
 */
int isp_tuning_get_contrast(isp_tuning_hd_t *hd, unsigned char *contrast);

/**
 * @fn int isp_tuning_set_contrast(isp_tuning_hd_t *hd, unsigned char contrast)
 *
 * 设置ISP 综合效果图片对比度
 *
 * @param[in] contrast 图片对比度参数
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @remark 默认值为128，大于128增加对比度，小于128降低对比度。
 *
 * @attention 在使用这个函数之前，必须保证ISP出图.
 */
int isp_tuning_set_contrast(isp_tuning_hd_t *hd, unsigned char contrast);

/**
 * @fn int isp_tuning_get_saturation(isp_tuning_hd_t *hd, unsigned char *saturation)
 *
 * 获取ISP 综合效果图片饱和度
 *
 * @param[in] sat 图片饱和度参数指针
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @remark 默认值为128，大于128增加饱和度，小于128降低饱和度。
 *
 * @attention 在使用这个函数之前，必须保证ISP出图.
 */
int isp_tuning_get_saturation(isp_tuning_hd_t *hd, unsigned char *saturation);

/**
 * @fn int isp_tuning_set_saturation(isp_tuning_hd_t *hd, unsigned char saturation)
 *
 * 设置ISP 综合效果图片饱和度
 *
 * @param[in] sat 图片饱和度参数值
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @remark 默认值为128，大于128增加饱和度，小于128降低饱和度。
 *
 * @attention 在使用这个函数之前，必须保证ISP出图.
 */
int isp_tuning_set_saturation(isp_tuning_hd_t *hd, unsigned char saturation);

/**
 * @fn int isp_tuning_get_sharpness(isp_tuning_hd_t *hd, unsigned char *sharpness)
 *
 * 获取ISP 综合效果图片锐度
 *
 * @param[in] sharpness 图片锐度参数指针
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @remark 默认值为128，大于128增加锐度，小于128降低锐度。
 *
 * @attention 在使用这个函数之前，必须保证ISP出图.
 */
int isp_tuning_get_sharpness(isp_tuning_hd_t *hd, unsigned char *sharpness);

 /**
 * @fn int isp_tuning_set_sharpness(isp_tuning_hd_t *hd, unsigned char sharpness)
 *
 * 设置ISP 综合效果图片锐度
 *
 * @param[in] sharpness 图片锐度参数值
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @remark 默认值为128，大于128增加锐度，小于128降低锐度。
 *
 * @attention 在使用这个函数之前，必须保证ISP出图.
 */
int isp_tuning_set_sharpness(isp_tuning_hd_t *hd, unsigned char sharpness);

/**
 * @fn int isp_tuning_get_anti_flicker_attr(isp_tuning_hd_t *hd, isp_anti_flicker_attr *attr)
 *
 * 获得ISP抗闪频属性
 *
 * @param[in] attr 获取参数值指针
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图.
 */
int isp_tuning_get_anti_flicker_attr(isp_tuning_hd_t *hd, isp_anti_flicker_attr *attr);

/**
 * @fn int isp_tuning_set_anti_flicker_attr(isp_tuning_hd_t *hd, isp_anti_flicker_attr attr)
 *
 * 设置ISP抗闪频属性
 *
 * @param[in] attr 设置参数值
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图.
 */
int isp_tuning_set_anti_flicker_attr(isp_tuning_hd_t *hd, isp_anti_flicker_attr attr);

/**
* @fn int isp_tuning_get_ev_attr(isp_tuning_hd_t *hd, isp_ev_attr *attr)
*
* 获取EV属性。
* @param[out] attr EV属性参数
*
* @retval 0 成功
* @retval 非0 失败，返回错误码
*
* @attention 在使用这个函数之前，必须保证ISP出图。
*/
int isp_tuning_get_ev_attr(isp_tuning_hd_t *hd, isp_ev_attr *attr);

/**
 * @fn int isp_tuning_get_expr(isp_tuning_hd_t *hd, isp_expr *expr)
 *
 * 获取AE参数。
 *
 * @param[out] expr AE参数。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_expr(isp_tuning_hd_t *hd, isp_expr *expr);

/**
 * @fn int isp_tuning_set_expr(isp_tuning_hd_t *hd, isp_expr *expr)
 *
 * 设置AE参数。
 *
 * @param[in] expr AE参数。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_set_expr(isp_tuning_hd_t *hd, isp_expr *expr);

/**
 * @fn int isp_tuning_get_max_again(isp_tuning_hd_t *hd, uint32_t *gain)
 *
 * 获取sensor可以设置最大Again。
 *
 * @param[out] gain sensor可以设置的最大again.0表示1x，32表示2x，依次类推。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_max_again(isp_tuning_hd_t *hd, uint32_t *gain);

/**
 * @fn int isp_tuning_set_max_again(isp_tuning_hd_t *hd, uint32_t gain)
 *
 * 设置sensor可以设置最大Again。
 *
 * @param[in] gain sensor可以设置的最大again.0表示1x，32表示2x，依次类推。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_set_max_again(isp_tuning_hd_t *hd, uint32_t gain);

/**
 * @fn int isp_tuning_get_max_dgain(isp_tuning_hd_t *hd, uint32_t *gain)
 *
 * 获取ISP设置的最大Dgain。
 *
 * @param[out] ISP Dgain 可以得到设置的最大的dgain.0表示1x，32表示2x，依次类推。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_max_dgain(isp_tuning_hd_t *hd, uint32_t *gain);

/**
 * @fn int isp_tuning_set_max_dgain(isp_tuning_hd_t *hd, uint32_t gain)
 *
 * 设置ISP可以设置的最大Dgain。
 *
 * @param[in] ISP Dgain 可以设置的最大dgain.0表示1x，32表示2x，依次类推。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_set_max_dgain(isp_tuning_hd_t *hd, uint32_t gain);

/**
 * @fn int isp_tuning_get_total_gain(isp_tuning_hd_t *hd, uint32_t *gain)
 *
 * 获取ISP输出图像的整体增益值
 *
 * @param[in] gain 获取增益值参数的指针,其数据存放格式为[24.8]，高24bit为整数，低8bit为小数。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_total_gain(isp_tuning_hd_t *hd, uint32_t *gain);

/**
 * @fn int isp_tuning_get_ae_min(isp_tuning_hd_t *hd, isp_ae_min *ae_min)
 *
 * 获取AE最小值参数。
 *
 * @param[out] ae_min AE最小值信息。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_ae_min(isp_tuning_hd_t *hd, isp_ae_min *ae_min);

/**
 * @fn int isp_tuning_set_ae_min(isp_tuning_hd_t *hd, isp_ae_min *ae_min)
 *
 * 设置AE最小值参数。
 *
 * @param[in] ae_min AE最小值参数。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_set_ae_min(isp_tuning_hd_t *hd, isp_ae_min *ae_min);

/**
* @fn int isp_tuning_get_ae_luma(isp_tuning_hd_t *hd, uint8_t *luma)
*
* 获取画面平均亮度。
*
* @param[out] luma AE亮度参数
*
* @retval 0 成功
* @retval 非0 失败，返回错误码
*
* @attention 在使用这个函数之前，必须保证ISP出图。
*/
int isp_tuning_get_ae_luma(isp_tuning_hd_t *hd, uint8_t *luma);

/**
 * @fn int isp_tuning_get_hi_light_depress(isp_tuning_hd_t *hd, uint32_t *strength)
 *
 * 获取强光抑制的强度。
 *
 * @param[out] strength 可以得到设置的强光抑制的强度.0表示关闭此功能。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_hi_light_depress(isp_tuning_hd_t *hd, uint32_t *strength);

/**
 * @fn int isp_tuning_set_hi_light_depress(isp_tuning_hd_t *hd, uint32_t strength)
 *
 * 设置强光抑制强度。
 *
 * @param[in] strength 强光抑制强度参数.取值范围为［0-10], 0表示关闭功能。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_set_hi_light_depress(isp_tuning_hd_t *hd, uint32_t strength);

/**
 * @fn int isp_tuning_get_ae_zone(isp_tuning_hd_t *hd, isp_zone *ae_zone)
 *
 * 获取AE各个zone的Y值。
 *
 * @param[out] ae_zone AE各个区域的Y值。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_ae_zone(isp_tuning_hd_t *hd, isp_zone *ae_zone);

/**
 * @fn int isp_tuning_get_ae_hist(isp_tuning_hd_t *hd, isp_ae_hist *ae_hist)
 *
 * 获取AE统计值。
 *
 * @param[out] ae_hist AE统计值信息。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_ae_hist(isp_tuning_hd_t *hd, isp_ae_hist *ae_hist);

/**
 * @fn int isp_tuning_set_ae_hist(isp_tuning_hd_t *hd, isp_ae_hist *ae_hist)
 *
 * 设置AE统计相关参数。
 *
 * @param[in] ae_hist AE统计相关参数。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_set_ae_hist(isp_tuning_hd_t *hd, isp_ae_hist *ae_hist);

/**
 * @fn int isp_tuning_get_ae_roi(isp_tuning_hd_t *hd, isp_weight *roi_weight)
 *
 * 获取AE感兴趣区域，用于场景判断。
 *
 * @param[out] roi_weight AE感兴趣区域权重。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_ae_roi(isp_tuning_hd_t *hd, isp_weight *roi_weight);

/**
 * @fn int isp_tuning_set_ae_roi(isp_tuning_hd_t *hd, isp_weight *roi_weight)
 *
 * 获取AE感兴趣区域，用于场景判断。
 *
 * @param[in] roi_weight AE感兴趣区域权重。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_set_ae_roi(isp_tuning_hd_t *hd, isp_weight *roi_weight);

/**
 * @fn int isp_tuning_get_ae_weight(isp_tuning_hd_t *hd, isp_weight *ae_weight)
 *
 * 获取AE统计区域的权重。
 *
 * @param[out] ae_weight 各区域权重信息。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_ae_weight(isp_tuning_hd_t *hd, isp_weight *ae_weight);

/**
 * @fn int isp_tuning_set_ae_weight(isp_tuning_hd_t *hd, isp_weight *ae_weight)
 *
 * 设置AE统计区域的权重。
 *
 * @param[in] ae_weight 各区域权重信息。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_set_ae_weight(isp_tuning_hd_t *hd, isp_weight *ae_weight);

/**
 * @fn int isp_tuning_set_wb(isp_tuning_hd_t *hd, isp_wb *wb)
 *
 * 设置白平衡功能设置。可以设置自动与手动模式，手动模式主要通过设置rgain、bgain实现。
 *
 * @param[in] wb 设置的白平衡参数。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_set_wb(isp_tuning_hd_t *hd, isp_wb *wb);

/**
 * @fn int isp_tuning_get_wb(isp_tuning_hd_t *hd, isp_wb *wb)
 *
 * 获取白平衡功能设置。
 *
 * @param[out] wb 获取的白平衡参数。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_wb(isp_tuning_hd_t *hd, isp_wb *wb);

/**
 * @fn int isp_tuning_get_wb_statis(isp_tuning_hd_t *hd, isp_wb *wb)
 *
 * 获取白平衡统计值。
 *
 * @param[out] wb 获取的白平衡统计值。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_wb_statis(isp_tuning_hd_t *hd, isp_wb *wb);

/**
 * @fn int isp_tuning_get_wb_gol_statis(isp_tuning_hd_t *hd, isp_wb *wb)
 *
 * 获取白平衡全局统计值。
 *
 * @param[out] wb 获取的白平衡全局统计值。
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_wb_gol_statis(isp_tuning_hd_t *hd, isp_wb *wb);

/**
* @fn int isp_tuning_get_gamma(isp_tuning_hd_t *hd, isp_gamma *gamma)
*
* 获取GAMMA参数.
* @param[out] gamma gamma参数
*
* @retval 0 成功
* @retval 非0 失败，返回错误码
*
* @attention 在使用这个函数之前，必须保证ISP出图。
*/
int isp_tuning_get_gamma(isp_tuning_hd_t *hd, isp_gamma *gamma);

/**
* @fn int isp_tuning_set_gamma(isp_tuning_hd_t *hd, isp_gamma *gamma)
*
* 设置GAMMA参数.
* @param[in] gamma gamma参数
*
* @retval 0 成功
* @retval 非0 失败，返回错误码
*
* @attention 在使用这个函数之前，必须保证ISP出图。
*/
int isp_tuning_set_gamma(isp_tuning_hd_t *hd, isp_gamma *gamma);

/**
 * @fn int isp_tuning_get_adr_strength(isp_tuning_hd_t *hd, uint32_t *strength)
 *
 * 获取ADR强度值.
 *
 * @param[out] strength DRC强度.
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_get_adr_strength(isp_tuning_hd_t *hd, uint32_t *strength);

/**
 * @fn int isp_tuning_set_adr_strength(isp_tuning_hd_t *hd, uint32_t strength)
 *
 * 设置ADR强度值.
 *
 * @param[in] strength DRC强度.
 *
 * @retval 0 成功
 * @retval 非0 失败，返回错误码
 *
 * @attention 在使用这个函数之前，必须保证ISP出图。
 */
int isp_tuning_set_adr_strength(isp_tuning_hd_t *hd, uint32_t strength);


#endif /* __IMP_ISP_H__ */