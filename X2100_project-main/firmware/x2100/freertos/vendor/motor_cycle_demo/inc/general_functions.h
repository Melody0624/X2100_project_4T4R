/*
 * @file        general_functions.h
 * @brief       通用数学工具函数接口。提供窗函数(Hanning/Blackman)、矩阵运算、
 *              数组统计分析、坐标变换、抛物线插值等常用工具的声明。
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#ifndef GENERALFUNC_H
#define	GENERALFUNC_H

#include "radar_types.h"
#include "../../mmw_msg_pkt/mmw_msg_pkt.h"

#include <stdint.h>
#include <sys/time.h>

// 饱和转换宏（安全，避免多次求值副作用）
#define SATURATE_UINT16(x)    \
    ({                        \
        float _x = (x);       \
        if (_x > 65535.0f)    \
            _x = 65535.0f;    \
        else if (_x < 0.0f)   \
            _x = 0.0f;        \
        (uint16_t)roundf(_x); \
    })

#define SATURATE_INT16(x)        \
    ({                           \
        float _x = (x);          \
        if (_x > 32767.0f)       \
            _x = 32767.0f;       \
        else if (_x < -32768.0f) \
            _x = -32768.0f;      \
        (int16_t)roundf(_x);     \
    })

#define SATURATE_UINT8(x)     \
    ({                        \
        float _x = roundf(x); \
        if (_x > 255.0f)      \
            _x = 255.0f;      \
        else if (_x < 0.0f)   \
            _x = 0.0f;        \
        (uint8_t)_x;          \
    })

// 带缩放因子的组合宏
#define TO_UINT16_SCALE(val, scale) SATURATE_UINT16((val) * (scale))
#define TO_INT16_SCALE(val, scale) SATURATE_INT16((val) * (scale))
#define TO_UINT8_INT(val) SATURATE_UINT8(val)

#ifdef __cplusplus
extern "C" {
#endif

uint64_t get_time_ns(void);
long timeval_diff_ms(struct timeval *start, struct timeval *end);
	typedef enum {
		TYPE_INT,   // 对应 int
		TYPE_FLOAT  // 对应 float
	} DataType;

	// 弧度转角度
	float rad2deg(float x);
	// 角度转弧度
	float deg2rad(float x);
	// 获取数的符号
	int signf(float x);
	// 查找数组中最大值的索引
	int find_max_peak(const float* x, int n);
	// 计算大于等于n的最小2的幂次方
	unsigned int nextPowerOfTwo(unsigned int n);
	// 计算hanning窗
	void hanning_window(float* window, int length);
	// 计算Blackman窗
	void blackman_window(float* window, int length);
	// 计算协方差矩阵
	void compute_covariance_matrix(const float* data, int numPts, int numDims, float* cov, int pool_index);
	// 计算2D矩阵的特征向量&特征值
	void eigendecomposition_2x2(const float* cov, float* eigenvalues, float* eigenvectors);
	// 计算任意大小矩阵的逆矩阵
	void inv_matrix(float* A, float* Ainv, int n, int pool_index);
	// 计算单精度浮点数组的最小值（SIMD加速）
	float array_min(const float* arr, size_t n);
	// 计算单精度浮点数组的均值（SIMD加速）
	float array_mean(const float* arr, size_t n);
	//计算单精度浮点数组的最大值（SIMD加速）
	float array_max(const float* arr, size_t n);
	// 计算单精度浮点数组(组合)的最大值（SIMD加速）
	float find_value_at_max_index(const float* A, const float* B, const int len);
	// 二项插值（均匀自变量）
	float parabolic_interp(const float* x, int n, int k);
	// 三点抛物线插值求峰值对应的自变量
	float parab_peak(const float* x, const float* y, int n, int k, int cyclic);
	// 点到直线距离 (标准化)
	float point_to_line_dist(float x, float y, float coeffA, float coeffB);
	// 计算 2x2 矩阵的逆矩阵
	bool inv2x2(const float* A, float* Ainv);
	// 交换两个 float 变量的值
	void swap_float(float* a, float* b);
	void swap_int(int* a, int* b);
	// 快速排序（双数组）
	void quicksort_floatArr(float A[], float B[], int low, int high, int ascending);
	void quicksort_intArr(int A[], int B[], int low, int high, int ascending);
	void quicksort_floatIntArr(float A[], int B[], int low, int high, int ascending);

#ifdef __cplusplus
}
#endif

#endif // !GENERALFUNC_H
