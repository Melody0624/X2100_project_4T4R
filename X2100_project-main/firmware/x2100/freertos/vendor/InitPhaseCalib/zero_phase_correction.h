/**
 * @file    zero_phase_correction.h
 * @brief   零度幅相校正系数计算（C实现）
 *
 * 对应 MATLAB: Func/getMeasAngADC.m 第179-180行
 *   zeroPhase_calib_vec = 1 ./ mean_tar_bv;
 *   zeroPhase_calib_vec = zeroPhase_calib_vec / norm(zeroPhase_calib_vec);
 *
 * 物理意义:
 *   对各虚拟通道的复波束向量取倒数（幅度取倒数 + 相位取反），
 *   再对整体向量做L2归一化。校正后各通道幅相一致，总功率为1。
 */

#ifndef ZERO_PHASE_CORRECTION_H
#define ZERO_PHASE_CORRECTION_H

#include "radar_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 根据预平均的波束向量计算零度幅相校正系数
 *
 * @param mean_tar_bv  [in]  预平均后的复波束向量（对应 MATLAB 的 mean_tar_bv），
 *                           长度 = num_ch。注意：C侧不需要再做帧间平均，
 *                           调用前已完成 mean(tar_bv_vec(:,5:15), 2) 等价操作。
 * @param num_ch       [in]  虚拟通道数（BSIS_Sample_A: 4TX×4RX = 16）
 * @param calib_vec    [out] 零度幅相校正系数，长度 = num_ch
 *
 * @note 算法两步融合为单趟循环，避免冗余计算：
 *       第1趟: (a) 计算复倒数 1/z_i  (b) 利用恒等式 |1/z|^2 = 1/|z|^2 累加范数平方
 *       第2趟: 乘法归一化（rsqrt模式，比逐元素除法更高效）
 */
void compute_zero_phase_correction(const fft_cpx_f32* mean_tar_bv,
                                   int num_ch,
                                   fft_cpx_f32* calib_vec);

#ifdef __cplusplus
}
#endif

#endif /* ZERO_PHASE_CORRECTION_H */
