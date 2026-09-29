/**********************************************************************************
 * Copyright: Copyright © 2025 SenardMicro All Rights Reserved.
 * @Date: : 2026-06-17
 * @Description: 板级标定配置统一管理
 *               通过 BOARD_TYPE 宏切换大板型，再通过对应变体宏选择具体硬件版本
 * Version: V1.0.1
 **********************************************************************************/
#ifndef BOARD_CALIB_CONFIG_H
#define BOARD_CALIB_CONFIG_H

#include "radar_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 板型大类定义
 * ================================================================ */
#define BOARD_TYPE_TWO_LAYER   0            /* 两层板 (2026.05.18 x2100) */
#define BOARD_TYPE_32X32       1            /* 32x32 板子 (X2100_1 ~ X2100_5) */
#define BOARD_TYPE_LONG_STRIP  2            /* 长条板 (x2100-8#, x2100-9# ...) */

// 32*32 板子编号定义
#define BOARD_32X32_ID_X2100_1 1            /* 32x32 板子 X2100_1 */
#define BOARD_32X32_ID_X2100_2 2            /* 32x32 板子 X2100_2 */
#define BOARD_32X32_ID_X2100_3 3            /* 32x32 板子 X2100_3 */
#define BOARD_32X32_ID_X2100_4 4            /* 32x32 板子 X2100_4 */
#define BOARD_32X32_ID_X2100_5 5            /* 32x32 板子 X2100_5 */
#define BOARD_32X32_ID_X2100_13 13          /* 32x32 板子 X2100_13 */

// 长条板编号定义
#define BOARD_LONG_STRIP_ID_X2100_1 1       /* 长条板 x2100-1# */
#define BOARD_LONG_STRIP_ID_X2100_2 2       /* 长条板 x2100-2# */
#define BOARD_LONG_STRIP_ID_X2100_3 3       /* 长条板 x2100-3# */
#define BOARD_LONG_STRIP_ID_X2100_4 4       /* 长条板 x2100-4# */
#define BOARD_LONG_STRIP_ID_X2100_5 5       /* 长条板 x2100-5# */
#define BOARD_LONG_STRIP_ID_X2100_6 6       /* 长条板 x2100-6# */
#define BOARD_LONG_STRIP_ID_X2100_7 7       /* 长条板 x2100-7# */
#define BOARD_LONG_STRIP_ID_X2100_8 8       /* 长条板 x2100-8# */
#define BOARD_LONG_STRIP_ID_X2100_9 9       /* 长条板 x2100-9# */
#define BOARD_LONG_STRIP_ID_X2100_10 10     /* 长条板 x2100-10# */
#define BOARD_LONG_STRIP_ID_MT4T4R_TX01 100 /* MT-4T4R-01, TX0/TX1 only */


/* [!] 在此选择当前板型 */
// #define BOARD_TYPE BOARD_TYPE_TWO_LAYER
// #define BOARD_TYPE  BOARD_TYPE_32X32
#define BOARD_TYPE BOARD_TYPE_LONG_STRIP

/* ================================================================
 * 各板型变体编号定义（小判断）
 * 仅当对应 BOARD_TYPE 被选中时，该宏才有效。
 * ================================================================ */
/* ----- 两层板（2026.05.18 x2100） ----- */
#if BOARD_TYPE == BOARD_TYPE_TWO_LAYER
#define BOARD_ID_TWO_LAYER 0
#endif

/* ----- 32x32 板子（1~5） ----- */
#if BOARD_TYPE == BOARD_TYPE_32X32
#define BOARD_ID_32X32 BOARD_32X32_ID_X2100_13 /* 选择 X2100_13 */
#endif

/* ----- 长条板（以 x2100-<编号#> 区分） ----- */
#if BOARD_TYPE == BOARD_TYPE_LONG_STRIP
#define BOARD_ID_LONG_STRIP BOARD_LONG_STRIP_ID_MT4T4R_TX01
#endif

/* ================================================================
 * 全局变量声明
 * ================================================================ */
extern fft_cpx_f32 angCalibMat[8]; // 雷达角度校准矩阵
extern fft_cpx_f32 angCalibMat_back[8];
extern float angFFT_interp[128];

/* ================================================================
 * 初始化函数
 * ================================================================ */
void board_calib_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_CALIB_CONFIG_H */
