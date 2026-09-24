/**
 * @file        general_functions.c
 * @brief       通用数学工具函数实现。提供 FFT 窗函数(Hanning/Blackman)、SIMD 加速
 *              数组统计(均值/最大/最小值)、矩阵运算、2x2 特征分解及抛物线插值等工具。
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <immintrin.h>
#define USE_SSE
#endif
#include "general_functions.h"
#include "memory_pool.h"
#include "../../mmw_msg_pkt/mmw_msg_pkt.h"


// 获取当前时间（纳秒）
uint64_t get_time_ns(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000000ULL + (uint64_t)tv.tv_usec * 1000ULL;
}

// 计算时间差的工具函数（单位：毫秒）
long timeval_diff_ms(struct timeval *start, struct timeval *end) {
    return (end->tv_sec - start->tv_sec) * 1000 +
           (end->tv_usec - start->tv_usec) / 1000;
}

/**
 * @brief 计算大于等于n的最小2的幂次方
 *
 * 该函数用于返回不小于n的最小2的幂。例如，输入5返回8，输入16返回16。
 * 常用于FFT等需要2的幂长度的场景。
 *
 * @param n 输入的无符号整数
 * @return unsigned int 大于等于n的最小2的幂
 */
unsigned int nextPowerOfTwo(unsigned int n) {
	if (n == 0) return 1;

	unsigned int power = 1;
	while (power < n) {
		power <<= 1;  // 左移一位相当于乘以2
	}
	return power;
}

/**
 * @brief 弧度转角度
 *
 * 将输入的弧度值转换为角度值。常用于雷达信号处理中的角度计算。
 *
 * @param x 输入的弧度值
 * @return float 转换后的角度值
 */
float rad2deg(float x) {
	return x * 180.0f / PI;
}

/**
 * @brief 角度转弧度
 *
 * 将输入的角度值转换为弧度值。常用于三角函数计算等场景。
 *
 * @param x 输入的角度值
 * @return float 转换后的弧度值
 */
float deg2rad(float x) {
	return x * PI / 180.0f;
}

/**
 * @brief 查找数组中最大值的索引
 *
 * 在长度为n的浮点数组x中查找最大值，并返回其索引。
 * 若有多个最大值，返回第一个出现的索引。
 *
 * @param x 输入的浮点数组指针
 * @param n 数组长度
 * @return int 最大值的索引
 */
int find_max_peak(const float* x, int n)
{
	int idx = 0;
	float maxv = x[0];

	for (int i = 1; i < n; i++) {
		if (x[i] > maxv) {
			maxv = x[i];
			idx = i;
		}
	}
	return idx;
}

/*
 * @brief 交换两个整数的值
 *
 * 通过指针交换两个整数变量的值。
 *
 * @param a 指向第一个整数的指针
 * @param b 指向第二个整数的指针
 */
void swap(int* a, int* b) {
	int temp = *a;
	*a = *b;
	*b = temp;
}

void swapf(float* a, float* b) {
	float temp = *a;
	*a = *b;
	*b = temp;
}
/*
 * @brief 获取数的符号
 *
 * 返回输入浮点数的符号：
 * - 返回1表示正数
 * - 返回-1表示负数
 * - 返回0表示零
 *
 * @param x 输入的浮点数
 * @return int 数的符号
 */
int signf(float x)
{
	return (x > 0) - (x < 0);
}

/*
 *  @brief 矩阵乘法 C = A * B
 * A: m x n, B: n x p, C: m x p
 */
void multiply_matrices(float* A, float* B, float* C, int m, int n, int p)
{
	int i, j, k;

	for (i = 0; i < m; i++) {
		for (j = 0; j < p; j++) {
			C[i * p + j] = 0.0f;
			for (k = 0; k < n; k++) {
				C[i * p + j] += A[i * n + k] * B[k * p + j];
			}
		}
	}
}

/*
 *  @brief 矩阵求逆 (高斯消元法)
 *
 */
void inv_matrix(float* A, float* Ainv, int n, int pool_index)
{
	float* aug = NULL;
	float pivot, factor;
	int i, j, k;

	aug = g_memoryPool[pool_index].kalman_aug;
	/* 检查内存池是否已初始化 */
	if (!g_memoryPool[pool_index].initialized) {
		fprintf(stderr, "matInverse: 内存池未初始化\n");
		return;
	}
	/* 检查矩阵大小是否超过最大限制 */
	unsigned int maxMeasMatLen = MAX_DETS_FOR_TRK_UPDATE + 2;
	if (n > maxMeasMatLen) {
		fprintf(stderr, "matInverse: 矩阵大小(%d)超过最大限制(%d)\n", n, maxMeasMatLen);
		return;
	}

	if (!aug) return;

	memset(aug, 0, maxMeasMatLen * 2 * maxMeasMatLen * sizeof(float));

	/* 构建增广矩阵 [A | I] */
	for (i = 0; i < n; i++) {
		for (j = 0; j < n; j++) {
			aug[i * 2 * n + j] = A[i * n + j];
			aug[i * 2 * n + n + j] = (i == j) ? 1.0f : 0.0f;
		}
	}

	/* 高斯消元 */
	for (i = 0; i < n; i++) {
		/* 选主元 */
		int maxRow = i;
		float maxVal = fabsf(aug[i * 2 * n + i]);
		for (k = i + 1; k < n; k++) {
			if (fabsf(aug[k * 2 * n + i]) > maxVal) {
				maxVal = fabsf(aug[k * 2 * n + i]);
				maxRow = k;
			}
		}

		/* 交换行 */
		if (maxRow != i) {
			for (j = 0; j < 2 * n; j++) {
				float temp = aug[i * 2 * n + j];
				aug[i * 2 * n + j] = aug[maxRow * 2 * n + j];
				aug[maxRow * 2 * n + j] = temp;
			}
		}

		pivot = aug[i * 2 * n + i];
		if (fabsf(pivot) < 1e-10f) {
			/* 矩阵奇异 */
			// free(aug);
			return;
		}

		/* 归一化当前行 */
		for (j = 0; j < 2 * n; j++) {
			aug[i * 2 * n + j] /= pivot;
		}

		/* 消元 */
		for (k = 0; k < n; k++) {
			if (k != i) {
				factor = aug[k * 2 * n + i];
				for (j = 0; j < 2 * n; j++) {
					aug[k * 2 * n + j] -= factor * aug[i * 2 * n + j];
				}
			}
		}
	}

	/* 提取逆矩阵 */
	for (i = 0; i < n; i++) {
		for (j = 0; j < n; j++) {
			Ainv[i * n + j] = aug[i * 2 * n + n + j];
		}
	}

}

/**
 * @brief 计算协方差矩阵（无偏估计，除以N-1）
 * 时间复杂度: O(N * D^2)
 *
 * @param data      输入数据，numDims个维度/feature，每个维度有numPts个数据点。按维度存储，依次存储每个维度的所有数据点，
 *                  即data[featureIdx * numPts + ptIdx]访问第featureIdx维度的第ptIdx个数据点。
 * @param numPts    数据点数量
 * @param numDims   数据维度
 * @param cov       输出协方差矩阵，形状[numDims][numDims]
 */
void compute_covariance_matrix(const float* data, int numPts, int numDims, float* cov, int pool_index) {
	// 参数检查
	if (numPts <= 1 || cov == NULL)
		return;

	// 检查内存池是否已初始化
	if (!g_memoryPool[pool_index].initialized) {
		fprintf(stderr, "compute_covariance_matrix: 内存池未初始化\n");
		return;
	}

	// 检查维度是否超过预分配缓冲区大小
	if (numDims > 2) {
		fprintf(stderr, "compute_covariance_matrix: 维度(%d)超过预分配缓冲区大小(2)\n", numDims);
		return;
	}

	// 使用全局内存池中的预分配缓冲区
	// 计算均值（按列存储）
	float* mean = g_memoryPool[pool_index].cov_mean;

	for (int featureIdx = 0; featureIdx < numDims; featureIdx++) {
		float sum = 0.0f;
		const float* featureData = data + featureIdx * numPts;
		for (int ptIdx = 0; ptIdx < numPts; ptIdx++) {
			sum += featureData[ptIdx];
		}
		mean[featureIdx] = sum / (float)numPts;
	}

	// 初始化协方差矩阵为零
	for (int idx = 0; idx < numDims * numDims; idx++) {
		cov[idx] = 0.0f;
	}

	// 为每个样本的偏差预分配临时数组
	float* diff = g_memoryPool[pool_index].cov_diff;

	// 计算协方差（只计算上三角）
	for (int ptIdx = 0; ptIdx < numPts; ptIdx++) {
		// 计算当前样本的每个特征的偏差
		for (int featureIdx = 0; featureIdx < numDims; featureIdx++) {
			diff[featureIdx] = data[featureIdx * numPts + ptIdx] - mean[featureIdx];
		}

		// 计算偏差的外积并累加（上三角）
		for (int feature1Idx = 0; feature1Idx < numDims; feature1Idx++) {
			//float diff1 = diff[feature1Idx];
			int offset = feature1Idx * numDims;
			for (int feature2Idx = feature1Idx; feature2Idx < numDims; feature2Idx++) {
				cov[offset + feature2Idx] += diff[feature1Idx] * diff[feature2Idx];
			}
		}
	}

	// 5. 填充对称部分并除以N-1（无偏估计）
	float scale = 1.0f / (float)(numPts - 1);
	for (int feature1Idx = 0; feature1Idx < numDims; feature1Idx++) {
		int diagOffset = feature1Idx * numDims + feature1Idx;
		cov[diagOffset] *= scale;
		for (int feature2Idx = feature1Idx + 1; feature2Idx < numDims; feature2Idx++) {
			float val = cov[feature1Idx * numDims + feature2Idx] * scale;
			cov[feature1Idx * numDims + feature2Idx] = val;
			cov[feature2Idx * numDims + feature1Idx] = val;
		}
	}

}

/**
 * @brief 2x2协方差矩阵特征值分解
 * @warning 仅适用于2x2矩阵，超过部分会被忽略
 * @param cov 2x2协方差矩阵，按行存储[c00, c01, c10, c11]
 * @param eigenvalues 输出特征值，lambda1 >= lambda2
 * @param eigenvectors 输出特征向量，按列存储[v1_x, v2_x; v1_y, v2_y]
 */
void eigendecomposition_2x2(const float* cov, float* eigenvalues, float* eigenvectors) {
	if (!cov || !eigenvalues || !eigenvectors) {
		fprintf(stderr, "错误: eigendecomposition_2x2 参数为空\n");
		return;
	}

	float a = cov[0];
	float b = cov[1];
	float c = cov[3];  // 注意：cov[2]是cov[1][0]，理论上等于b

	// 使用更稳定的特征值计算方法
	float trace = a + c;
	float det = a * c - b * b;

	float d = trace * trace - 4.0f * det;
	// 使用max避免负数
	float sqrt_d = sqrtf(fmaxf(0.0f, d));

	eigenvalues[0] = (trace + sqrt_d) * 0.5f;
	eigenvalues[1] = (trace - sqrt_d) * 0.5f;

	// 确保顺序
	if (eigenvalues[0] < eigenvalues[1]) {
		float temp = eigenvalues[0];
		eigenvalues[0] = eigenvalues[1];
		eigenvalues[1] = temp;
	}

	// 计算特征向量
	float lambda1 = eigenvalues[0];
	const float eps = 1e-7f;

	if (fabsf(b) > eps) {
		// 非对角占优情况
		eigenvectors[0] = b;
		eigenvectors[2] = lambda1 - a;
	}
	else {
		// 近对角矩阵
		eigenvectors[0] = 1.0f;
		eigenvectors[2] = 0.0f;
	}

	// 归一化第一个特征向量
	float norm1 = sqrtf(eigenvectors[0] * eigenvectors[0] + eigenvectors[2] * eigenvectors[2]);
	if (norm1 > eps) {
		eigenvectors[0] /= norm1;
		eigenvectors[2] /= norm1;
	}

	// 第二个特征向量正交于第一个
	eigenvectors[1] = -eigenvectors[2];
	eigenvectors[3] = eigenvectors[0];

	// 不强制行列式为+1，保持与特征值的对应关系
}

/**
 * 计算Hanning窗（汉宁窗）- 与MATLAB的hanning函数一致
 * MATLAB: w = hanning(L)
 * @param window 预先分配的存储窗函数结果的数组指针（float类型）
 * @param length 窗函数长度（int类型）
 */
void hanning_window(float* window, int length) {
	if (window == NULL || length <= 0) {
		return; // 无效参数
	}

	if (length == 1) {
		window[0] = 1.0f;
		return;
	}

	for (int n = 0; n < length; n++) {
		// MATLAB Hanning窗公式: w(n) = 0.5 * (1 - cos(2*pi*n/(L-1)))
		// 注意：MATLAB索引从1开始，但C索引从0开始，公式相同
		float arg = 2.0f * (float)PI * (n + 1) / (length + 1);
		window[n] = 0.5f * (1.0f - cosf(arg));
	}
}

/**
 * 计算Blackman窗（布莱克曼窗）- 与MATLAB的blackman函数一致
 * MATLAB: w = blackman(L)
 * @param window 预先分配的存储窗函数结果的数组指针（float类型）
 * @param length 窗函数长度（int类型）
 */
void blackman_window(float* window, int length) {
	if (window == NULL || length <= 0) {
		return; // 无效参数
	}

	if (length == 1) {
		window[0] = 1.0f;
		return;
	}

	// MATLAB Blackman窗系数（与标准Blackman窗一致）
	float a0 = 0.42f;
	float a1 = 0.5f;
	float a2 = 0.08f;

	for (int n = 0; n < length; n++) {
		// MATLAB Blackman窗公式: w(n) = 0.42 - 0.5*cos(2*pi*n/(L-1)) + 0.08*cos(4*pi*n/(L-1))
		float arg1 = 2.0f * (float)PI * n / (length - 1);
		float arg2 = 4.0f * (float)PI * n / (length - 1);
		window[n] = a0 - a1 * cosf(arg1) + a2 * cosf(arg2);
	}
}

/**
 * @brief 计算单精度浮点数组的均值（SIMD加速）
 *
 * 使用SSE指令集进行并行计算，一次处理4个浮点数。
 * 对于长度小于4的数组，自动回退到普通循环处理。
 *
 * @param arr 输入数组指针
 * @param n 数组长度（可以为0）
 * @return float 数组的算术平均值，n=0时返回0.0f
 *
 * @note 要求平台支持SSE指令集
 * @see _mm_loadu_ps, _mm_add_ps
 */
float array_mean(const float* arr, size_t n) {
	if (n == 0) return 0.0f;

#ifdef USE_SSE
	if (n < 4) {
		float sum = 0.0f;
		for (size_t j = 0; j < n; j++) {
			sum += arr[j];
		}
		return sum / n;
	}

	__m128 sum_vec = _mm_setzero_ps();
	size_t i = 0;

	for (; i <= n - 4; i += 4) {
		__m128 data = _mm_loadu_ps(&arr[i]);
		sum_vec = _mm_add_ps(sum_vec, data);
	}

	float sum_array[4];
	_mm_storeu_ps(sum_array, sum_vec);
	float sum = sum_array[0] + sum_array[1] + sum_array[2] + sum_array[3];

	for (; i < n; i++) {
		sum += arr[i];
	}

	return sum / n;
#else
    // 普通 C 实现
	float sum = 0.0f;
	for (size_t j = 0; j < n; j++) {
		sum += arr[j];
	}
	return sum / n;
#endif
}

/**
 * @brief 计算单精度浮点数组的最大值（SIMD加速）
 *
 * 使用SSE指令集进行并行比较，一次处理4个浮点数。
 * 对于长度小于4的数组，自动回退到普通循环处理。
 *
 * @param arr 输入数组指针
 * @param n 数组长度（可以为0）
 * @return float 数组中的最大值，n=0时返回-FLT_MAX
 *
 * @note 要求平台支持SSE指令集
 * @see _mm_loadu_ps, _mm_max_ps
 */
float array_max(const float* arr, size_t n) {
	if (n == 0) return -FLT_MAX;

#ifdef USE_SSE
	if (n < 4) {
		float max_val = arr[0];
		for (size_t j = 1; j < n; j++) {
			if (arr[j] > max_val) max_val = arr[j];
		}
		return max_val;
	}

	__m128 max_vec = _mm_set1_ps(-FLT_MAX);
	size_t i = 0;

	for (; i <= n - 4; i += 4) {
		__m128 data = _mm_loadu_ps(&arr[i]);
		max_vec = _mm_max_ps(max_vec, data);
	}

	float max_array[4];
	_mm_storeu_ps(max_array, max_vec);
	float max_val = max_array[0];
	for (int j = 1; j < 4; j++) {
		if (max_array[j] > max_val) max_val = max_array[j];
	}

	for (; i < n; i++) {
		if (arr[i] > max_val) max_val = arr[i];
	}

	return max_val;
#else
	// 普通 C 实现
	float max_val = arr[0];
	for (size_t j = 1; j < n; j++) {
		if (arr[j] > max_val) max_val = arr[j];
	}
	return max_val;
#endif
}

/**
 * @brief 计算单精度浮点数组的最小值（SIMD加速）
 *
 * 使用SSE指令集进行并行比较，一次处理4个浮点数。
 * 对于长度小于4的数组，自动回退到普通循环处理。
 *
 * @param arr 输入数组指针
 * @param n 数组长度（可以为0）
 * @return float 数组中的最小值，n=0时返回FLT_MAX
 *
 * @note 要求平台支持SSE指令集
 * @see _mm_loadu_ps, _mm_min_ps
 */
float array_min(const float* arr, size_t n) {
	if (n == 0) return FLT_MAX;

#ifdef USE_SSE
	if (n < 4) {
		float min_val = arr[0];
		for (size_t j = 1; j < n; j++) {
			if (arr[j] < min_val) min_val = arr[j];
		}
		return min_val;
	}

	__m128 min_vec = _mm_set1_ps(FLT_MAX);
	size_t i = 0;

	for (; i <= n - 4; i += 4) {
		__m128 data = _mm_loadu_ps(&arr[i]);
		min_vec = _mm_min_ps(min_vec, data);
	}

	float min_array[4];
	_mm_storeu_ps(min_array, min_vec);
	float min_val = min_array[0];
	for (int j = 1; j < 4; j++) {
		if (min_array[j] < min_val) min_val = min_array[j];
	}

	for (; i < n; i++) {
		if (arr[i] < min_val) min_val = arr[i];
	}

	return min_val;
#else
	// 普通 C 实现
	float min_val = arr[0];
	for (size_t j = 1; j < n; j++) {
		if (arr[j] < min_val) min_val = arr[j];
	}
	return min_val;
#endif
}

/**
 * 在数组A中找到最大值的索引，返回数组B中该索引对应的值。
 *
 * @param A 指向数组A的指针
 * @param B 指向数组B的指针
 * @param len 指向数组长度的指针（长度必须为正数）
 * @return 若长度有效且数组非空，返回B中与A最大值同索引的值；
 *         若长度无效（NULL或非正数），返回0（可根据需求修改）
 */
float find_value_at_max_index(const float* A, const float* B, const int len) {
	// 检查长度指针及长度是否有效
	if (len <= 0) {
		return 0;  // 无效输入时返回默认值（可根据业务调整）
	}

	int max_index = 0;		// 记录最大值索引，初始为0
	float max_val = A[0];	// 记录最大值，初始为第一个元素

	// 遍历数组A，寻找最大值及其索引
	for (int i = 1; i < len; ++i) {
		if (A[i] > max_val) {
			max_val = A[i];
			max_index = i;
		}
	}

	// 返回数组B中对应索引的值
	return B[max_index];
}

/**
 * @brief  三点抛物线插值求峰值对应的自变量
 * @param  x      自变量序列（如角度），长度 n
 * @param  y      因变量序列（如幅度），长度 n
 * @param  n      序列长度
 * @param  k      峰值索引
 * @param  cyclic 1=周期翻折（循环取左右点），0=边界不插值直接返回 x[k]
 * @return 插值后的精确自变量值
 */
float parab_peak(const float* x, const float* y, int n, int k, int cyclic)
{
	int km1, kp1;
	if (cyclic) {
		km1 = (k - 1 + n) % n;
		kp1 = (k + 1) % n;
	}
	else {
		if (k <= 0 || k >= n - 1) return x[k];
		km1 = k - 1;
		kp1 = k + 1;
	}

	float x0 = x[km1], x1 = x[k], x2 = x[kp1];
	float y0 = y[km1], y1 = y[k], y2 = y[kp1];

	float dx10 = x1 - x0, dx21 = x2 - x1;
	if (fabsf(dx10) < 1e-9f || fabsf(dx21) < 1e-9f) return x1;

	float s1 = (y1 - y0) / dx10;
	float s2 = (y2 - y1) / dx21;
	float A = (s2 - s1) / (x2 - x0);          // 二次项系数
	if (A >= 0.0f) return x1;                 // 非极大值，退化

	float B = s1 - A * (x1 + x0);             // 一次项系数
	float x_peak = -B / (2.0f * A);

	// 钳位在相邻自变量区间内
	float x_min = fminf(x0, x2);
	float x_max = fmaxf(x0, x2);
	if (x_peak < x_min) x_peak = x_min;
	if (x_peak > x_max) x_peak = x_max;

	return x_peak;
}

/**
 * @brief 计算二维平面上两点之间的加权欧几里得距离
 *
 * 距离公式为：
 *   d = sqrt( weight_x * (x1 - x2)^2 + weight_y * (y1 - y2)^2 )
 * 权重用于缩放各坐标轴方向上的差异，权重越大，该方向上的差异对最终距离的影响越显著。
 *
 * @param x1       第一个点的 X 坐标
 * @param y1       第一个点的 Y 坐标
 * @param x2       第二个点的 X 坐标
 * @param y2       第二个点的 Y 坐标
 * @param weight_x X 轴方向的权重，通常 >= 0
 * @param weight_y Y 轴方向的权重，通常 >= 0
 * @return 加权后的欧几里得距离
 */
float weighted_euclidean_dist(float x1, float y1,
	float x2, float y2,
	float weight_x, float weight_y)
{
	float dx = x1 - x2;
	float dy = y1 - y2;
	return sqrtf(weight_x * dx * dx + weight_y * dy * dy);
}

/**
 * @brief 计算二维平面上两点之间的标准欧几里得距离
 *
 * 等效于 X 和 Y 轴权重均为 1.0 的加权欧几里得距离，即
 *   d = sqrt( (x1 - x2)^2 + (y1 - y2)^2 )
 *
 * @param x1 第一个点的 X 坐标
 * @param y1 第一个点的 Y 坐标
 * @param x2 第二个点的 X 坐标
 * @param y2 第二个点的 Y 坐标
 * @return 两点之间的直线距离
 */
float euclidean_dist(float x1, float y1, float x2, float y2)
{
	return weighted_euclidean_dist(x1, y1, x2, y2, 1.0f, 1.0f);
}

/**
 * @brief 计算点到直线的垂直距离
 *
 * 直线方程为 y = coeffA * x + coeffB，即 coeffA * x - y + coeffB = 0。
 * 点 (x, y) 到该直线的距离公式为：
 *   d = |coeffA * x - y + coeffB| / sqrt(coeffA^2 + 1)
 *
 * @param x      点的 X 坐标
 * @param y      点的 Y 坐标
 * @param coeffA 直线方程中的斜率系数
 * @param coeffB 直线方程中的截距系数
 * @return 点到直线的垂直距离（非负）
 */
float point_to_line_dist(float x, float y, float coeffA, float coeffB)
{
	return fabsf(coeffA * x - y + coeffB) / sqrtf(coeffA * coeffA + 1.0f);
}

/**
 * @brief 计算 2x2 矩阵的逆矩阵
 *
 * 输入矩阵 A 按行优先存储为长度为 4 的数组：
 *   A = [a, b; c, d] 对应 A[0]=a, A[1]=b, A[2]=c, A[3]=d
 * 当行列式 |det| >= 1e-12 时，计算逆矩阵存入 Ainv，同样按行优先存储。
 * 若行列式过小（接近奇异），则返回 false 且不修改 Ainv。
 *
 * @param A    输入矩阵，长度为 4，行优先排列
 * @param Ainv 输出逆矩阵，长度为 4，行优先排列；仅在返回 true 时有效
 * @return true  求逆成功
 * @return false 矩阵奇异或近似奇异，无法求逆
 */
bool inv2x2(const float* A, float* Ainv)
{
	float det = A[0] * A[3] - A[1] * A[2];
	if (fabsf(det) < 1e-12f) return false;

	float invDet = 1.0f / det;
	Ainv[0] = A[3] * invDet;
	Ainv[1] = -A[1] * invDet;
	Ainv[2] = -A[2] * invDet;
	Ainv[3] = A[0] * invDet;
	return true;
}

/*
 * Block RLS 参数更新
 * θ_new = θ_prev + K * (Y - X*θ_prev)
 * K = P_old * X' / (λI + X*P_old*X')
 * P_new = (I - K*X) * P_old / λ
 *
 * X_block: [x1, 1; x2, 1; ...]  (numRows × 2, row-major)
 * Y_block: [y1; y2; ...]         (numRows × 1)
 * theta:   [k; b]               (2 × 1)
 * P:       2×2                  (row-major)
 */
void block_rls_update(const float* theta_prev, const float* P_old,
	const float* X_block, const float* Y_block, int numRows,
	float lambda, float* theta_new, float* P_new)
{
	if (numRows <= 0 || lambda <= 0.0f) {
		/* 无数据或无效lambda，保持原值 */
		theta_new[0] = theta_prev[0];
		theta_new[1] = theta_prev[1];
		memcpy(P_new, P_old, 4 * sizeof(float));
		return;
	}

	/* 计算 X*P_old*X' (numRows × numRows)，但对角矩阵块RLS:
	 * 对每个数据点逐点更新 (更稳定) */
	float P[4];
	float theta[2];
	memcpy(P, P_old, 4 * sizeof(float));
	theta[0] = theta_prev[0];
	theta[1] = theta_prev[1];

	for (int i = 0; i < numRows; i++) {
		float xi = X_block[i * 2];       /* x */
		float one = X_block[i * 2 + 1];   /* 1 */
		float yi = Y_block[i];

		/* S = X*P*X' + λ (标量, 因为X是1x2) */
		float S = lambda
			+ xi * (P[0] * xi + P[1] * one)
			+ one * (P[2] * xi + P[3] * one);

		if (fabsf(S) < 1e-10f) continue;

		/* K = P * X' / S  (2x1) */
		float K0 = (P[0] * xi + P[1] * one) / S;
		float K1 = (P[2] * xi + P[3] * one) / S;

		/* error = y - X*theta */
		float err = yi - (xi * theta[0] + one * theta[1]);

		/* theta_new = theta + K * error */
		theta[0] += K0 * err;
		theta[1] += K1 * err;

		/* P_new = (I - K*X) * P / lambda */
		float P00 = P[0], P01 = P[1], P10 = P[2], P11 = P[3];
		P[0] = ((1.0f - K0 * xi) * P00 - K0 * one * P10) / lambda;
		P[1] = ((1.0f - K0 * xi) * P01 - K0 * one * P11) / lambda;
		P[2] = ((-K1 * xi) * P00 + (1.0f - K1 * one) * P10) / lambda;
		P[3] = ((-K1 * xi) * P01 + (1.0f - K1 * one) * P11) / lambda;
	}

	theta_new[0] = theta[0];
	theta_new[1] = theta[1];
	memcpy(P_new, P, 4 * sizeof(float));
}