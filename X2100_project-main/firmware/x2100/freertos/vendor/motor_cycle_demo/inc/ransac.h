/**
 * @file        ransac.h
 * @brief       通用 RANSAC 算法模块接口。定义 RANSAC 配置/结果结构体、拟合/距离
 *              函数指针类型，提供初始化、执行(ransac_run)及默认配置获取等函数的声明。
 * @author      Jie Wu
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */

/*
 * ransac.h
 * 通用 RANSAC 算法模块
 */

#ifndef RANSAC_H
#define RANSAC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

	/* RANSAC 默认配置 */
#define RANSAC_DEFAULT_MAX_TRIALS       999
#define RANSAC_DEFAULT_CONFIDENCE       0.99f
#define RANSAC_DEFAULT_SAMPLE_SIZE      2
#define RANSAC_DEFAULT_MODEL_SIZE       2
  /* RANSAC 默认配置 */
  #define RANSAC_DEFAULT_MAX_TRIALS       999
  #define RANSAC_DEFAULT_CONFIDENCE       0.99f
  #define RANSAC_DEFAULT_SAMPLE_SIZE      2
  #define RANSAC_DEFAULT_MODEL_SIZE       2

  /* RANSAC 内部 buffer 容量上限（所有 caller 取最大值）*/
  #define RANSAC_MAX_SAMPLE_SIZE          4   /* >= max(vlc=2, static_zone=3) */
  #define RANSAC_MAX_MODEL_SIZE           4   /* >= max(vlc model=2, line model=2) */
  #define RANSAC_MAX_NUM_DATA             MAX_STATIC_DET_INFO_REC  /* = 300，涵蓋 static_zone 與 vlc */

	/* RANSAC 配置结构体 */
	typedef struct {
		int sampleSize;         /* 最小样本点数 (默认: 2) */
		int maxNumTrials;       /* 最大迭代次数 (默认: 999) */
		float confidence;       /* 置信度 (默认: 0.99) */
		float maxDistance;      /* 内点阈值 */
	} RansacConfig;

	/* RANSAC 结果结构体 */
	typedef struct {
		float* model;           /* 模型参数数组 (由调用者分配) */
		int modelSize;          /* 模型参数数量 */
		bool* inlierMask;       /* 内点掩码数组 (由调用者分配) */
		int numInliers;         /* 内点数量 */
		int numIterations;      /* 实际迭代次数 */
		bool isMaxAttempts;     /* 是否达到最大迭代次数 */
		bool success;           /* 是否成功 */
	} RansacResult;

	/*
	 * 模型拟合函数指针类型
	 * @param data          数据数组
	 * @param dataStride    每个数据点的步长 (字节数或元素数)
	 * @param indices       采样索引数组
	 * @param numSamples    采样点数
	 * @param model         输出模型参数
	 * @param userData      用户自定义数据
	 * @return              true 成功, false 失败
	 */
	typedef bool (*RansacFitFunc)(
		const void* data,
		int dataStride,
		const int* indices,
		int numSamples,
		float* model,
		void* userData
		);

	/*
	 * 距离计算函数指针类型
	 * @param data          数据数组
	 * @param dataStride    每个数据点的步长
	 * @param dataIndex     数据点索引
	 * @param model         模型参数
	 * @param userData      用户自定义数据
	 * @return              该点到模型的距离
	 */
	typedef float (*RansacDistFunc)(
		const void* data,
		int dataStride,
		int dataIndex,
		const float* model,
		void* userData
		);

	/*
	 * ransac_init - 初始化 RANSAC 模块
	 * @param seed  随机数种子
	 */
	void ransac_init(unsigned int seed);

	/*
	 * ransac_resetRandomIndex - 重置随机数索引
	 */
	void ransac_reset_random_index(void);


	/*
	 * ransac_run - 执行 RANSAC 算法
	 * @param data          数据数组
	 * @param dataStride    每个数据点的步长
	 * @param numData       数据点数量
	 * @param config        RANSAC 配置
	 * @param fitFunc       模型拟合函数
	 * @param distFunc      距离计算函数
	 * @param userData      用户自定义数据 (传递给 fitFunc 和 distFunc)
	 * @param result        输出结果 (调用者需预先分配 model 和 inlierMask)
	 * @return              true 成功, false 失败
	 */
	bool ransac_run(
		const void* data,
		int dataStride,
		int numData,
		const RansacConfig* config,
		RansacFitFunc fitFunc,
		RansacDistFunc distFunc,
		void* userData,
		RansacResult* result,
		int pool_index
	);

	/*
	 * ransac_get_default_config - 获取默认配置
	 * @param config        输出配置
	 */
	void ransac_get_default_config(RansacConfig* config);

#ifdef __cplusplus
}
#endif

#endif /* RANSAC_H */
