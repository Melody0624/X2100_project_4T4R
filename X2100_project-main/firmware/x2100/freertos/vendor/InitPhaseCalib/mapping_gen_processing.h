/**
 * @file    mapping_gen_processing.h
 * @brief   角度映射关系生成 —— 将雷达(测量角,真实角)数据对转换为角度FFT修正表
 *
 * 前置条件: 输入数据已由调用者完成清洗 (无NaN, 按gt升序排序, 已去重)
 *
 * 算法流程:
 *   1. 以0°为锚点,向两侧寻找最长单调递增/递减子序列 (LIS/LDS)
 *   2. 在标准角度FFT网格上做PCHIP插值,生成修正映射表 angFFT_mod
 *
 * 物理含义:
 *   angFFT_mod[i] = 第i个角度FFT bin对应的"校正后角度(度)"
 *   测角时直接查表 angFFT_mod[peak_bin] 即可获得修正后的角度值
 */

#ifndef MAPPING_GEN_PROCESSING_H
#define MAPPING_GEN_PROCESSING_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 生成角度映射修正表
 * 
 * @param angMeasGT_gt     [in]  地面真实角度数组 (度), 长度 gt_len
 * @param gt_len           [in]  gt 数组长度
 * @param angMeasGT_meas   [in]  雷达测量角度数组 (度), 长度 meas_len
 * @param meas_len         [in]  meas 数组长度
 * @param angFFT_mod       [out] 修正后的角度FFT映射表, 调用者预分配 n_fft 个 float
 * @param n_fft            [in]  角度FFT点数 (如128)
 * @return  0: 成功
 *         -1: 输入参数无效 (NULL指针, 长度不匹配, n_fft<=0)
 *         -2: 有效数据点不足 (NaN过多或去重后 < 2点)
 *         -3: 未找到0°锚点, 无法确定单调区间
 *         -4: 单调子序列为空
 */
int mappingGen_processing(
    const float *angMeasGT_gt,  int gt_len,
    const float *angMeasGT_meas, int meas_len,
    float *angFFT_mod,           int n_fft
);
/* Signed physical spacing, in wavelengths, for the current PCB routing. */
int mappingGen_processing_with_spacing(
    const float *gt, int gt_len, const float *meas, int meas_len,
    float *output, int n_fft, float spacing_wavelengths);

#ifdef __cplusplus
}
#endif

#endif /* MAPPING_GEN_PROCESSING_H */
