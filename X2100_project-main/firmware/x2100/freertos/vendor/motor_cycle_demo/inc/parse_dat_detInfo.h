/**
 * @file        parse_dat_detInfo.h
 * @brief       TLV格式 .dat 文件检测信息解析器接口。定义消息头常量、Magic Word、
 *              TLV Tag 标识及检测信息解析函数 parse_dat_detInfo 的声明。
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#ifndef PARSE_DAT_DETINFO_H
#define PARSE_DAT_DETINFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "radar_types.h"

	/*=============================================================================
	 *  TLV 消息格式常量 (源自 parse_x2000_tlv_v2.m)
	 *============================================================================*/
#define MSG_HEADER_NUM_UINT32      7       // 消息头包含 9 个 uint32 (36 字节)
#define MSG_HEADER_BYTES           (MSG_HEADER_NUM_UINT32 * 4)

#define MAGIC_WORD_0               0x03040102u
#define MAGIC_WORD_1               0x07080506u

	 /* TLV Tag 定义 */
#define TLV_TAG_DETINFO            21u     // 检测点信息
#define TLV_TAG_TRACKINFO          22u     // 航迹信息
#define TLV_TAG_EGO_VLC            23u     // 自车速度
#define TLV_TAG_WARNINFO           24u     // 告警信息

/* 检测点二进制字段数量 (每帧写入的字段数, 不含内存中额外字段) */
#define DETOBJ_BINARY_FIELDS       15
#define DETOBJ_BINARY_BYTES        (DETOBJ_BINARY_FIELDS * 4)  // 60 字节/检测点

/*=============================================================================
 *  函数接口
 *============================================================================*/

 /**
  * @brief 从已打开的 TLV 格式 .dat 文件中读取一帧数据.
  *
  *  isParseDatDetInfo 控制解析/跳帧行为 (参考 get_adc_from_dat_file.c 模式):
  *    - true  : 遍历所有 TLV, 仅解析 DETINFO_TAG(21) 并填充 ctx->detInfo,
  *              其余 TLV Tag (TRACKINFO/EGO_VLC/WARNINFO) 自动跳过 payload.
  *    - false : 仅读取 msg_header 并跳过所有 TLV payload, 不做任何解析,
  *              仅移动文件指针到下一帧起始位置.
  *
  * @param isParseDatDetInfo  是否解析检测点信息
  * @param fid                已用 "rb" 打开的 FILE* 指针
  * @param ctx                全局上下文指针, 解析后的 detInfo 写入 ctx->detInfo
  * @return int
  *     0  = 成功
  *    -1  = 文件结束 (EOF)
  *    -2  = 数据格式错误 (magic word 不匹配等)
  */
	int parse_dat_detInfo(FILE* fid, GlbCtx* ctx, bool isSkipFrm);

#ifdef __cplusplus
}
#endif

#endif // PARSE_DAT_DETINFO_H
