/**
 * @file    zero_phase_correction.c
 * @brief   零度幅相校正系数计算 —— 单趟高效实现
 *
 * 对应 MATLAB 逻辑（getMeasAngADC.m:179-180）:
 *   zeroPhase_calib_vec = 1 ./ mean_tar_bv;
 *   zeroPhase_calib_vec = zeroPhase_calib_vec / norm(zeroPhase_calib_vec);
 *
 * 设计要点（非盲从翻译，按C特点优化）:
 *   1. 单趟循环同时完成"复倒数"和"范数平方累加"
 *      - 利用恒等式 |1/z|² = 1/|z|²，不需要二次遍历计算 norm
 *   2. 归一化用 inv_norm = 1/sqrt(norm²) 乘法代替逐元素除法
 *   3. 无分支、无查表，适合编译器自动向量化（-O3 -march=native）
 */

#include "zero_phase_correction.h"
#include <math.h>

void compute_zero_phase_correction(const fft_cpx_f32* mean_tar_bv,
                                   int num_ch,
                                   fft_cpx_f32* calib_vec)
{
    float norm_sq = 0.0f;

    /* ==================================================================
     * Pass 1: 逐通道计算复倒数，同时累加 ||calib||²
     *
     * 复倒数公式: 1/(a + b*i) = (a - b*i) / (a² + b²)
     *
     * 关键优化:
     *   |calib_i|² = |1/z_i|² = 1/|z_i|² = 1/(a²+b²) = inv_mag_sq
     *   因此无需存储 calib 后再平方，直接复用 inv_mag_sq 累加即可。
     *   Pass 1 结束后 norm_sq = Σ|calib_i|² 已就绪。
     * ================================================================== */
    for (int i = 0; i < num_ch; ++i) {
        const float re = mean_tar_bv[i].r;
        const float im = mean_tar_bv[i].i;
        const float mag_sq = re * re + im * im;

        // 一次除法，两处复用: (1) 复倒数分子 (2) 范数累加
        const float inv_mag_sq = 1.0f / mag_sq;

        calib_vec[i].r =  re * inv_mag_sq;   // 实部:  a / (a²+b²)
        calib_vec[i].i = -im * inv_mag_sq;   // 虚部: -b / (a²+b²)

        norm_sq += inv_mag_sq;                 // 累加 |1/z_i|²
    }

    /* ==================================================================
     * Pass 2: L2 归一化
     *
     * calib /= ||calib||₂
     *
     * 使用 recip-sqrt 模式: 先算 1/√(norm_sq)，再逐元素乘法。
     * 对于 num_ch=16 的量级，比逐元素除法快约 2×（除法延迟 >> 乘法延迟）。
     * ================================================================== */
    const float inv_norm = 1.0f / sqrtf(norm_sq);

    for (int i = 0; i < num_ch; ++i) {
        calib_vec[i].r *= inv_norm;
        calib_vec[i].i *= inv_norm;
    }
}
