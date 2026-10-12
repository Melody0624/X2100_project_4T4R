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

/**
 * @brief 计算大于等于n的最小2的幂次方
 *
 * 该函数用于返回不小于n的最小2的幂。例如，输入5返回8，输入16返回16。
 * 常用于FFT等需要2的幂长度的场景。
 *
 * @param n 输入的无符号整数
 * @return unsigned int 大于等于n的最小2的幂
 */
unsigned int nextPowerOfTwo(unsigned int n)
{
	if (n == 0) return 1;

	unsigned int power = 1;
	while (power < n) {
		power <<= 1;  // 左移一位相当于乘以2
	}
	return power;
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
 * @brief   计算 Blackman 窗（布莱克曼窗），与 MATLAB blackman() 一致。
 *          公式: w[n] = 0.42 - 0.5*cos(2*pi*n/(L-1)) + 0.08*cos(4*pi*n/(L-1))
 * @param   window  输出窗系数数组（调用方预分配 length 个 float）
 * @param   length  窗长度
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
 * @brief 计算协方差矩阵（无偏估计，除以N-1）
 * 时间复杂度: O(N * D^2)
 *
 * @param data      输入数据，numDims个维度/feature，每个维度有numPts个数据点。按维度存储，依次存储每个维度的所有数据点，
 *                  即data[featureIdx * numPts + ptIdx]访问第featureIdx维度的第ptIdx个数据点。
 * @param numPts    数据点数量
 * @param numDims   数据维度
 * @param cov       输出协方差矩阵，形状[numDims][numDims]
 * @param pool      调用方提供的 scratch buffer，函数从该地址顺序划分 2*numDims 个 float
 *                  作为内部分布的暂存区（mean/diff），不释放、不改动调用方的池指针。
 * @warning         调用方须保证：
 *                  1) pool 可用空间 >= 2 * numDims * sizeof(float) 字节；
 *                  2) pool 4 字节对齐（函数内部按 float 访问）；
 *                  3) pool 区域不得与 data 或 cov 重叠，否则将静默覆盖输入/输出数据。
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
			int offset = feature1Idx * numDims;
			for (int feature2Idx = feature1Idx; feature2Idx < numDims; feature2Idx++) {
				cov[offset + feature2Idx] += diff[feature1Idx] * diff[feature2Idx];
			}
		}
	}

	// 填充对称部分并除以N-1（无偏估计）
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
 * @brief 计算单精度浮点数组的均值（SIMD加速）
 *
 * @param arr 输入数组指针
 * @param n 数组长度（可以为0）
 * @return float 数组的算术平均值，n=0时返回0.0f
 */
float array_mean(const float* arr, size_t n) {
	if (n == 0) return 0.0f;

	float sum = 0.0f;
	for (size_t j = 0; j < n; j++) {
		sum += arr[j];
	}
	return sum / n;
}

/**
 * @brief 计算单精度浮点数组的最小值（SIMD加速）
 *
 * @param arr 输入数组指针
 * @param n 数组长度（可以为0）
 * @return float 数组中的最小值，n=0时返回FLT_MAX
 */
float array_min(const float* arr, size_t n) {
	if (n == 0) return FLT_MAX;

	float min_val = arr[0];
	for (size_t j = 1; j < n; j++) {
		if (arr[j] < min_val) min_val = arr[j];
	}
	return min_val;
}



/**
 * @brief 计算单精度浮点数组的最大值（SIMD加速）
 *
 * @param arr 输入数组指针
 * @param n 数组长度（可以为0）
 * @return float 数组中的最大值，n=0时返回-FLT_MAX
 */

float array_max(const float* arr, size_t n) {
	if (n == 0) return -FLT_MAX;

	float max_val = arr[0];
	for (size_t j = 1; j < n; j++) {
		if (arr[j] > max_val) max_val = arr[j];
	}
	return max_val;
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
	if (len <= 0)
		return 0;

	int max_index = 0;
	float max_val = A[0];

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
 * @brief   抛物线插值，用于 FFT 峰值亚像元精确定位。
 *          在离散峰值点 k 处利用相邻三点 (k-1, k, k+1) 拟合抛物线，
 *          返回插值后的浮点索引。
 * @param   x  输入幅度数组
 * @param   n  数组长度
 * @param   k  峰值整数索引
 * @return  插值后的亚像元浮点索引
 */
float parabolic_interp(const float* x, int n, int k)
{
	int km1 = (k - 1 + n) % n;
	int kp1 = (k + 1) % n;

	float y1 = x[km1];
	float y2 = x[k];
	float y3 = x[kp1];

	float denom = (y1 - 2 * y2 + y3);
	if (fabsf(denom) < 1e-6f)
		return (float)k;

	return k + 0.5f * (y1 - y3) / denom;
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


/**
 * @brief 交换两个 float 变量的值
 * @param a 指向第一个 float 的指针
 * @param b 指向第二个 float 的指针
 */
void swap_float(float* a, float* b) {
	float t = *a;
	*a = *b;
	*b = t;
}

/**
 * @brief 根据数组 A 的大小，对 float 类型数组 A 和 B 进行同步快速排序（原地排序）
 *
 * @param A         主数组（float 类型），排序依据
 * @param B         辅助数组（float 类型），跟随 A 中元素的位置变化而同步重排
 * @param low       待排序区间的起始下标（包含）
 * @param high      待排序区间的结束下标（包含）
 * @param ascending 排序方向控制：1 表示升序，0 表示降序
 *
 * @note 采用 Hoare 分区法，选取区间中间元素作为基准（pivot）
 * @note 分区时使用双指针从两端向中间扫描，当两指针交错时停止
 * @note 递归调用时以右指针 j 作为分割点，避免栈溢出
 * @warning 要求 A 和 B 的长度相同，且 low <= high
 */
void quicksort_floatArr(float A[], float B[], int low, int high, int ascending) {
	if (low >= high) return;

	int mid = low + (high - low) / 2;
	float pivot = A[mid];
	int i = low - 1;
	int j = high + 1;

	while (1) {
		// 根据排序方向移动左指针：升序找小于pivot，降序找大于pivot
		do { i++; } while (ascending ? A[i] < pivot : A[i] > pivot);
		// 根据排序方向移动右指针：升序找大于pivot，降序找小于pivot
		do { j--; } while (ascending ? A[j] > pivot : A[j] < pivot);
		if (i >= j) break;

		// 交换 A 中不符合顺序的元素，同时交换 B 中对应位置的元素
		swap_float(&A[i], &A[j]);
		swap_float(&B[i], &B[j]);
	}

	// 递归排序左右两部分（注意：右区间从 j+1 开始）
	quicksort_floatArr(A, B, low, j, ascending);
	quicksort_floatArr(A, B, j + 1, high, ascending);
}

/**
 * @brief 交换两个 int 变量的值
 * @param a 指向第一个 int 的指针
 * @param b 指向第二个 int 的指针
 */
void swap_int(int* a, int* b) {
	int t = *a;
	*a = *b;
	*b = t;
}

/**
 * @brief 根据数组 A 的大小，对 int 类型数组 A 和 B 进行同步快速排序（原地排序）
 *
 * @param A         主数组（int 类型），排序依据
 * @param B         辅助数组（int 类型），跟随 A 中元素的位置变化而同步重排
 * @param low       待排序区间的起始下标（包含）
 * @param high      待排序区间的结束下标（包含）
 * @param ascending 排序方向控制：1 表示升序，0 表示降序
 *
 * @note 采用 Hoare 分区法，选取区间中间元素作为基准（pivot）
 * @note 分区时使用双指针从两端向中间扫描，当两指针交错时停止
 * @note 递归调用时以右指针 j 作为分割点，避免栈溢出
 * @warning 要求 A 和 B 的长度相同，且 low <= high
 */
void quicksort_intArr(int A[], int B[], int low, int high, int ascending) {
	if (low >= high) return;

	int mid = low + (high - low) / 2;
	int pivot = A[mid];
	int i = low - 1;
	int j = high + 1;

	while (1) {
		// 根据排序方向移动左指针：升序找小于pivot，降序找大于pivot
		do { i++; } while (ascending ? A[i] < pivot : A[i] > pivot);
		// 根据排序方向移动右指针：升序找大于pivot，降序找小于pivot
		do { j--; } while (ascending ? A[j] > pivot : A[j] < pivot);
		if (i >= j) break;

		// 交换 A 中不符合顺序的元素，同时交换 B 中对应位置的元素
		swap_int(&A[i], &A[j]);
		swap_int(&B[i], &B[j]);
	}

	// 递归排序左右两部分（注意：右区间从 j+1 开始）
	quicksort_intArr(A, B, low, j, ascending);
	quicksort_intArr(A, B, j + 1, high, ascending);
}

/**
 * @brief 以 float 主键数组 A 升/降序，int 副键数组 B 同步重排（Hoare 分区，原地）
 * @param A         主键数组（float），排序依据
 * @param B         副键数组（int），随 A 同步重排
 * @param low       待排序区间起始下标（含）
 * @param high      待排序区间结束下标（含）
 * @param ascending 1 升序，0 降序
 */
void quicksort_floatIntArr(float A[], int B[], int low, int high, int ascending) {
	if (low >= high) return;

	int mid = low + (high - low) / 2;
	float pivot = A[mid];
	int i = low - 1;
	int j = high + 1;

	while (1) {
		// 根据排序方向移动左右指针
		do { i++; } while (ascending ? A[i] < pivot : A[i] > pivot);
		do { j--; } while (ascending ? A[j] > pivot : A[j] < pivot);
		if (i >= j) break;

		// 同步交换主键与副键
		{ float t = A[i]; A[i] = A[j]; A[j] = t; }
		{ int   t = B[i]; B[i] = B[j]; B[j] = t; }
	}

	// 递归排序左右两部分（右区间从 j+1 开始）
	quicksort_floatIntArr(A, B, low, j, ascending);
	quicksort_floatIntArr(A, B, j + 1, high, ascending);
}