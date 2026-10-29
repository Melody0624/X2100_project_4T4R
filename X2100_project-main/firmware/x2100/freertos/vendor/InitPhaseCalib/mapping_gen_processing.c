
/**
 * @file    mapping_gen_processing.c
 * @brief   角度映射关系生成 —— 高效C实现
 *
 * 算法对照 MATLAB 原始逻辑, C语言优化要点:
 *   - LIS/LDS 使用DP+回溯, 预分配避免动态数组增长
 *   - PCHIP插值: 导数预计算一次, 查询仅做二分+Hermite多项式求值
 *   - 输入数据已由调用者完成清洗 (无NaN, 按gt排序去重)
 */

#include "mapping_gen_processing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ========================================================================
 *  内部辅助宏
 * ======================================================================== */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ========================================================================
 *  阶段1: 最长递增/递减子序列 (LIS / LDS)
 *  使用 O(n²) DP, 对于典型雷达数据集 (n≤数百) 足够高效
 *  ======================================================================== */

/**
 * @brief 最长严格递增子序列, 返回索引数组 (0-based, 内部malloc)
 * @param x      输入数组
 * @param n      数组长度
 * @param idx_out 输出: 递增子序列中各元素在x中的索引
 * @return 子序列长度, 0表示输入为空
 */
static int lis_index(const float *x, int n, int **idx_out) {
    if (n <= 0) { *idx_out = NULL; return 0; }

    int *dp   = (int *)malloc((size_t)n * sizeof(int));
    int *prev = (int *)malloc((size_t)n * sizeof(int));
    if (!dp || !prev) { free(dp); free(prev); *idx_out = NULL; return -1; }

    int max_len = 1, max_end = 0;
    for (int i = 0; i < n; ++i) {
        dp[i]   = 1;
        prev[i] = -1;
        for (int j = 0; j < i; ++j) {
            if (x[j] < x[i] && dp[j] + 1 > dp[i]) {
                dp[i]   = dp[j] + 1;
                prev[i] = j;
            }
        }
        if (dp[i] > max_len) {
            max_len = dp[i];
            max_end = i;
        }
    }

    /* 回溯构造索引序列 */
    int *idx = (int *)malloc((size_t)max_len * sizeof(int));
    if (!idx) { free(dp); free(prev); *idx_out = NULL; return -1; }

    int pos = max_end;
    for (int k = max_len - 1; k >= 0; --k) {
        idx[k] = pos;
        pos = prev[pos];
    }

    free(dp);
    free(prev);
    *idx_out = idx;
    return max_len;
}

/**
 * @brief 最长严格递减子序列, 返回索引数组 (0-based, 内部malloc)
 */
static int lds_index(const float *x, int n, int **idx_out) {
    if (n <= 0) { *idx_out = NULL; return 0; }

    int *dp   = (int *)malloc((size_t)n * sizeof(int));
    int *prev = (int *)malloc((size_t)n * sizeof(int));
    if (!dp || !prev) { free(dp); free(prev); *idx_out = NULL; return -1; }

    int max_len = 1, max_end = 0;
    for (int i = 0; i < n; ++i) {
        dp[i]   = 1;
        prev[i] = -1;
        for (int j = 0; j < i; ++j) {
            if (x[j] > x[i] && dp[j] + 1 > dp[i]) {
                dp[i]   = dp[j] + 1;
                prev[i] = j;
            }
        }
        if (dp[i] > max_len) {
            max_len = dp[i];
            max_end = i;
        }
    }

    int *idx = (int *)malloc((size_t)max_len * sizeof(int));
    if (!idx) { free(dp); free(prev); *idx_out = NULL; return -1; }

    int pos = max_end;
    for (int k = max_len - 1; k >= 0; --k) {
        idx[k] = pos;
        pos = prev[pos];
    }

    free(dp);
    free(prev);
    *idx_out = idx;
    return max_len;
}

/* ========================================================================
 *  阶段3: 单调子序列合并
 *  ======================================================================== */

/**
 * @brief 以0°锚点为中心, 合并左侧LDS + 右侧LIS
 * 
 * @param meas        排序去重后的测量角度数组
 * @param n           数组长度
 * @param zero_idx    0°锚点在meas中的索引 (gt==0的位置)
 * @param combined_idx_out  合并后的索引数组 (内部malloc)
 * @return 合并后索引数量, <=0表示失败
 */
static int merge_monotonic_subseq(
    const float *meas, int n, int zero_idx, int **combined_idx_out)
{
    /* 右侧: LIS from zero_idx to n-1 */
    int n_right = n - zero_idx;
    int *lis_idx = NULL;
    int lis_len = lis_index(meas + zero_idx, n_right, &lis_idx);
    if (lis_len < 0) return -1;

    /* LIS索引转回全局坐标 */
    for (int i = 0; i < lis_len; ++i) {
        lis_idx[i] += zero_idx;
    }

    /* 左侧: LDS from zero_idx down to 0 (反转数组) */
    int n_left = zero_idx + 1;  /* meas[0..zero_idx] */
    float *rev_left = (float *)malloc((size_t)n_left * sizeof(float));
    if (!rev_left) { free(lis_idx); return -1; }
    for (int i = 0; i < n_left; ++i) {
        rev_left[i] = meas[zero_idx - i];
    }

    int *lds_idx_in_rev = NULL;
    int lds_len = lds_index(rev_left, n_left, &lds_idx_in_rev);
    free(rev_left);
    if (lds_len < 0) { free(lis_idx); return -1; }

    /* LDS索引转回原始全局坐标: idx_global = zero_idx - idx_in_rev */
    int *lds_idx = (int *)malloc((size_t)lds_len * sizeof(int));
    if (!lds_idx) { free(lds_idx_in_rev); free(lis_idx); return -1; }
    for (int i = 0; i < lds_len; ++i) {
        lds_idx[i] = zero_idx - lds_idx_in_rev[i];
    }
    free(lds_idx_in_rev);

    /* 反转LDS索引使其递增 */
    for (int i = 0; i < lds_len / 2; ++i) {
        int tmp = lds_idx[i];
        lds_idx[i] = lds_idx[lds_len - 1 - i];
        lds_idx[lds_len - 1 - i] = tmp;
    }

    /* 合并 LDS + LIS, 处理重叠 */
    int *combined = NULL;
    int combined_len = 0;

    if (lds_len == 0 && lis_len == 0) {
        free(lds_idx); free(lis_idx);
        *combined_idx_out = NULL;
        return 0;
    }

    /* 预分配最大可能长度 */
    int max_len = lds_len + lis_len;
    combined = (int *)malloc((size_t)max_len * sizeof(int));
    if (!combined) { free(lds_idx); free(lis_idx); return -1; }

    if (lds_len == 0) {
        memcpy(combined, lis_idx, (size_t)lis_len * sizeof(int));
        combined_len = lis_len;
    } else if (lis_len == 0) {
        memcpy(combined, lds_idx, (size_t)lds_len * sizeof(int));
        combined_len = lds_len;
    } else {
        int last_lds  = lds_idx[lds_len - 1];
        int first_lis = lis_idx[0];

        if (last_lds < first_lis) {
            /* 完美衔接: LDS所有 + LIS所有 */
            memcpy(combined, lds_idx, (size_t)lds_len * sizeof(int));
            memcpy(combined + lds_len, lis_idx, (size_t)lis_len * sizeof(int));
            combined_len = lds_len + lis_len;
        } else if (last_lds == first_lis) {
            /* 重叠于锚点: 去掉LDS最后一个 */
            memcpy(combined, lds_idx, (size_t)(lds_len - 1) * sizeof(int));
            memcpy(combined + lds_len - 1, lis_idx, (size_t)lis_len * sizeof(int));
            combined_len = lds_len + lis_len - 1;
        } else {
            /* 理论上不应发生: 截取LDS中 < first_lis 的部分 */
            int split = -1;
            for (int i = 0; i < lds_len; ++i) {
                if (lds_idx[i] < first_lis) split = i;
            }
            if (split < 0) {
                memcpy(combined, lis_idx, (size_t)lis_len * sizeof(int));
                combined_len = lis_len;
            } else {
                memcpy(combined, lds_idx, (size_t)(split + 1) * sizeof(int));
                memcpy(combined + split + 1, lis_idx, (size_t)lis_len * sizeof(int));
                combined_len = (split + 1) + lis_len;
            }
        }
    }

    free(lds_idx);
    free(lis_idx);
    *combined_idx_out = combined;
    return combined_len;
}

/* ========================================================================
 *  阶段4: PCHIP 插值 (Piecewise Cubic Hermite Interpolating Polynomial)
 *  基于 Fritsch-Carlson 单调保形算法
 *  ======================================================================== */

/**
 * @brief 计算PCHIP节点导数 (Fritsch-Carlson方法)
 * 
 * @param x  自变量 (严格递增)
 * @param y  因变量
 * @param n  点数
 * @param d  输出: 各节点导数值 (调用者分配 n 个 float)
 */
static void pchip_derivatives(const float *x, const float *y, int n, float *d) {
    if (n <= 1) {
        if (n == 1) d[0] = 0.0f;
        return;
    }

    /* 差分与斜率 */
    float *h     = (float *)malloc((size_t)(n - 1) * sizeof(float));
    float *delta = (float *)malloc((size_t)(n - 1) * sizeof(float));
    if (!h || !delta) { free(h); free(delta); return; }

    for (int i = 0; i < n - 1; ++i) {
        h[i]     = x[i + 1] - x[i];
        delta[i] = (y[i + 1] - y[i]) / h[i];
    }

    /* 内部节点 (1 .. n-2) */
    for (int k = 1; k < n - 1; ++k) {
        if (delta[k - 1] * delta[k] <= 0.0f) {
            /* 斜率变号或为零 → 导数为0 (局部极值) */
            d[k] = 0.0f;
        } else {
            /* 加权调和平均 */
            float w1 = 2.0f * h[k] + h[k - 1];
            float w2 = h[k] + 2.0f * h[k - 1];
            d[k] = (w1 + w2) / (w1 / delta[k - 1] + w2 / delta[k]);
        }
    }

    /* 端点导数 (基于二次多项式外推, 加单调性约束) */
    /* 左端点 */
    {
        float h0 = h[0], h1 = (n > 2) ? h[1] : h0;
        float d0 = delta[0], d1 = (n > 2) ? delta[1] : d0;
        float ept = ((2.0f * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
        if (ept * d0 <= 0.0f) {
            d[0] = 0.0f;
        } else if ((d0 * d1 <= 0.0f) && (fabsf(ept) > 3.0f * fabsf(d0))) {
            d[0] = 3.0f * d0;
        } else {
            d[0] = ept;
        }
    }

    /* 右端点 */
    {
        int last = n - 1;
        float h_1 = h[last - 1], h_2 = (n > 2) ? h[last - 2] : h_1;
        float d_1 = delta[last - 1], d_2 = (n > 2) ? delta[last - 2] : d_1;
        float ept = ((2.0f * h_1 + h_2) * d_1 - h_1 * d_2) / (h_1 + h_2);
        if (ept * d_1 <= 0.0f) {
            d[last] = 0.0f;
        } else if ((d_1 * d_2 <= 0.0f) && (fabsf(ept) > 3.0f * fabsf(d_1))) {
            d[last] = 3.0f * d_1;
        } else {
            d[last] = ept;
        }
    }

    free(h);
    free(delta);
}

/**
 * @brief 单点PCHIP求值: 在有序节点 x[0..n-1] 上查找区间并做Hermite求值
 * 
 * @param x  自变量节点 (严格递增)
 * @param y  因变量节点
 * @param d  预计算的节点导数值
 * @param n  节点数
 * @param xi 查询点
 * @return pchip(xi)
 */
static float pchip_eval(const float *x, const float *y, const float *d, int n, float xi) {
    /* 边界处理 */
    if (xi <= x[0])   return y[0];
    if (xi >= x[n-1]) return y[n-1];

    /* 二分查找区间 [k, k+1) 包含 xi */
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) >> 1;
        if (x[mid] <= xi) lo = mid;
        else              hi = mid;
    }
    int k = lo;  /* x[k] <= xi < x[k+1] */

    /* Cubic Hermite 求值 */
    float hk  = x[k + 1] - x[k];
    float t   = (xi - x[k]) / hk;   /* t ∈ [0, 1) */
    float t2  = t * t;
    float t3  = t2 * t;

    /* Hermite基函数 */
    float H1 =  2.0f * t3 - 3.0f * t2 + 1.0f;   /* (1-t)²(1+2t) */
    float H2 =        t3 - 2.0f * t2 + t;        /* t(1-t)² */
    float H3 = -2.0f * t3 + 3.0f * t2;           /* t²(3-2t) */
    float H4 =        t3 -       t2;              /* t²(t-1) */

    return H1 * y[k] + H2 * hk * d[k] + H3 * y[k + 1] + H4 * hk * d[k + 1];
}

/* ========================================================================
 *  阶段5: FFT角度网格生成
 *  ======================================================================== */

/**
 * @brief 生成标准角度FFT网格
 *   angFFT[i] = asin( (i - n_fft/2) / (n_fft * minElemSpacing) ) * 180/pi
 *   其中 minElemSpacing = 0.5 (lambda/2 阵元间距)
 * 
 * @param angFFT  输出数组 (调用者预分配 n_fft 个 float)
 * @param n_fft   FFT点数
 */
static void build_angfft_grid(float *angFFT, int n_fft, float min_elem_spacing) {
    const float inv_n_half       = 1.0f / ((float)n_fft * min_elem_spacing);
    const float rad2deg          = 180.0f / (float)M_PI;
    const float half_n           = (float)n_fft * 0.5f;

    for (int i = 0; i < n_fft; ++i) {
        float arg = ((float)i - half_n) * inv_n_half;
        /* 数值裁剪到 [-1, 1] */
        if      (arg >  1.0f) arg =  1.0f;
        else if (arg < -1.0f) arg = -1.0f;
        angFFT[i] = asinf(arg) * rad2deg;
    }
}

/* ========================================================================
 *  主函数  n_fft = 128
 *  ======================================================================== */

int mappingGen_processing_with_spacing(
    const float *angMeasGT_gt,  int gt_len,
    const float *angMeasGT_meas, int meas_len,
    float *angFFT_mod,           int n_fft, float spacing_wavelengths)
{
    if (!isfinite(spacing_wavelengths) || fabsf(spacing_wavelengths) < 1e-6f) return -1;
    /* ---------- 参数校验 ---------- */
    if (!angMeasGT_gt || !angMeasGT_meas || !angFFT_mod) {
        fprintf(stderr, "[mappingGen_processing] ERROR: NULL pointer input.\n");
        return -1;
    }
    if (gt_len <= 0 || meas_len <= 0 || n_fft <= 0) {
        fprintf(stderr, "[mappingGen_processing] ERROR: invalid length (gt=%d, meas=%d, n_fft=%d).\n",
                gt_len, meas_len, n_fft);
        return -1;
    }
    if (gt_len != meas_len) {
        fprintf(stderr, "[mappingGen_processing] ERROR: gt_len(%d) != meas_len(%d).\n",
                gt_len, meas_len);
        return -1;
    }
    if (gt_len < 2) {
        fprintf(stderr, "[mappingGen_processing] ERROR: insufficient data points (%d), need >= 2.\n",
                gt_len);
        return -2;
    }

    /* ========== 阶段1: 查找0°锚点 ========== */
    int zero_idx = -1;
    {
        int lo = 0, hi = gt_len - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >> 1;
            if (angMeasGT_gt[mid] == 0.0f) {
                zero_idx = mid;
                break;
            } else if (angMeasGT_gt[mid] < 0.0f) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
    }
    if (zero_idx < 0) {
        fprintf(stderr, "[mappingGen_processing] ERROR: 0-degree anchor not found in gt array.\n");
        return -3;
    }

    /* ========== 阶段2: 最长单调子序列 ========== */
    int *combined_idx = NULL;
    int combined_len = merge_monotonic_subseq(angMeasGT_meas, gt_len, zero_idx, &combined_idx);
    if (combined_len < 0) {
        fprintf(stderr, "[mappingGen_processing] ERROR: memory allocation failed in LIS/LDS.\n");
        return -1;
    }
    if (combined_len < 2) {
        fprintf(stderr, "[mappingGen_processing] ERROR: monotonic subsequence too short (%d points).\n", combined_len);
        free(combined_idx);
        return -4;
    }

    /* 提取单调区间内的 (real=gt, est=meas) 数据 */
    float *x_thr = (float *)malloc((size_t)combined_len * sizeof(float));
    float *y_thr = (float *)malloc((size_t)combined_len * sizeof(float));
    if (!x_thr || !y_thr) {
        fprintf(stderr, "[mappingGen_processing] ERROR: malloc failed for x_thr/y_thr.\n");
        free(x_thr); free(y_thr);
        free(combined_idx);
        return -1;
    }
    for (int i = 0; i < combined_len; ++i) {
        int idx   = combined_idx[i];
        x_thr[i]  = angMeasGT_gt[idx];    /* ground truth → 插值的因变量 */
        y_thr[i]  = angMeasGT_meas[idx];  /* measured angle → 插值的自变量 */
    }
    free(combined_idx);

    /* ========== 阶段3: 构建FFT网格 ========== */
    build_angfft_grid(angFFT_mod, n_fft, spacing_wavelengths);

    /* ========== 阶段4: 确定插值范围 ========== */
    float y_min = y_thr[0];
    float y_max = y_thr[combined_len - 1];
    if (y_min > y_max) { float tmp = y_min; y_min = y_max; y_max = tmp; }

    /* 统计 angFFT 中落在 [y_min, y_max] 范围内的点数 */
    int sel_cnt = 0;
    for (int i = 0; i < n_fft; ++i) {
        if (angFFT_mod[i] >= y_min && angFFT_mod[i] <= y_max)
            ++sel_cnt;
    }

    if (sel_cnt > 0) {
        /* ========== 阶段5: PCHIP插值 ========== */
        /* interp1(y_thr, x_thr, angFFT_sel, 'pchip')
         *   自变量: y_thr (测量角)  → 因变量: x_thr (真实角) */

        typedef struct {
            float val;
            int   orig_idx;
        } QueryPoint;

        QueryPoint *queries = (QueryPoint *)malloc((size_t)sel_cnt * sizeof(QueryPoint));
        if (!queries) {
            fprintf(stderr, "[mappingGen_processing] ERROR: malloc failed for queries.\n");
            free(x_thr); free(y_thr);
            return -1;
        }

        int q = 0;
        for (int i = 0; i < n_fft; ++i) {
            if (angFFT_mod[i] >= y_min && angFFT_mod[i] <= y_max) {
                queries[q].val      = angFFT_mod[i];
                queries[q].orig_idx = i;
                ++q;
            }
        }

        /* 预计算PCHIP节点导数 (一次) */
        float *pchip_d = (float *)malloc((size_t)combined_len * sizeof(float));
        if (!pchip_d) {
            fprintf(stderr, "[mappingGen_processing] ERROR: malloc failed for pchip_d.\n");
            free(queries); free(x_thr); free(y_thr);
            return -1;
        }
        pchip_derivatives(y_thr, x_thr, combined_len, pchip_d);

        /* 逐点求值, 写回 angFFT_mod */
        for (int i = 0; i < sel_cnt; ++i) {
            angFFT_mod[queries[i].orig_idx] =
                pchip_eval(y_thr, x_thr, pchip_d, combined_len, queries[i].val);
        }

        free(pchip_d);
        free(queries);
    }
    /* sel_cnt==0: 无需插值, angFFT_mod 保持原始网格值 */

    /* ---------- 清理 ---------- */
    free(x_thr);
    free(y_thr);

    return 0;
}

/* Preserve the legacy entry point for any supplier archive caller. */
int mappingGen_processing(const float *gt, int gt_len, const float *meas,
                          int meas_len, float *output, int n_fft) {
    return mappingGen_processing_with_spacing(gt, gt_len, meas, meas_len,
                                              output, n_fft, 0.5f);
}
