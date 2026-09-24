/**
 * @file        get_adc_from_dat_file.c
 * @brief       ADC 原始数据读取模块。从 Cheetah 芯片二进制 .dat 文件中逐帧解析
 *              Packet Header、Chirp Header 及 ADC 采样数据，支持多子帧模式。
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "radar_functions.h"
#include "heap_malloc.h"

size_t get_adc_total_size(GlbCtx* ctx)
{
	return ctx->wave_params.numChirps * (sizeof(CheetahChirpHeadInfo) + (sizeof(uint16_t) * ctx->basic_params.numRxs * ctx->wave_params.adcLen) + CHIRP_END_BYTES);
}

#if 0
int get_adc_from_dat_file(GlbCtx* ctx, FILE* fid, bool isSkipFrm) {

	// 临时变量
	int sfIdx = 0;
	int cntSubFrame = 0;
	int adcLen = ctx->wave_params.adcLen;
	int numChirps = ctx->wave_params.numChirps;
	int numRxs = ctx->basic_params.numRxs;
	int numRXPerMMIC = ctx->basic_params.numRXPerMMIC;
	int numSubFrms = ctx->wave_params.numSubFrms;

	// 分配内存（如果尚未分配）
	size_t totalSamples = adcLen * numChirps * numRxs;
	if (ctx->adcData.sf1 == NULL) {
		ctx->adcData.sf1 = (float*)malloc(totalSamples * sizeof(float));
		if (ctx->adcData.sf1 == NULL) {
			fprintf(stderr, "内存分配失败\n");
			return -1;
		}
		// 初始化为零
		memset(ctx->adcData.sf1, 0, totalSamples * sizeof(float));
	}

	// 子帧循环
	while (cntSubFrame != numSubFrms) {
		/* ****** 读取Packet Header部分 ****** */
		// 跳过 Packet header（保留 header 中的 tag 和 length 两部分）
		if (fseek(fid, PACKET_HEADER_BYTES - 8, SEEK_CUR) != 0) {
			fprintf(stderr, "Packet header错误, 文件定位失败（Packet header）\n");
			return -1;
		}

		// 读取 tag（芯片标识）
		uint32_t tag;
		if (fread(&tag, sizeof(uint32_t), 1, fid) != 1) {
			fprintf(stderr, "读取 tag 失败\n");
			return -1;
		}

		// 读取 payload 长度
		uint32_t payloadLen;
		if (fread(&payloadLen, sizeof(uint32_t), 1, fid) != 1) {
			fprintf(stderr, "读取 payload 长度失败\n");
			return -1;
		}

		/* ******* 读取Chirp Header部分 ******* */
		// 读取 Chirp header（16 个 uint16）
		uint16_t chirpHeaders[16];
		if (fread(chirpHeaders, sizeof(uint16_t), CHIRP_HEADER_BYTES_CHEETAH / 2, fid) != CHIRP_HEADER_BYTES_CHEETAH / 2) {
			fprintf(stderr, "读取 Chirp header 失败\n");
			return -1;
		}

		// 确定子帧 ID（从 chirpHeaders[5] 中提取 bits 10‑11）
		sfIdx = ((chirpHeaders[5] >> 10) & 0x3) + 1;

		/* *********** 进阶信息计算 *********** */
		uint32_t packetAlignBytes = 16 - ((PACKET_HEADER_BYTES + payloadLen) % 16);	// 计算结尾空字节长度
		uint32_t payloadWordCount = (payloadLen - CHIRP_HEADER_BYTES_CHEETAH) / 2;	// 计算 payload 中 uint16 的数量
		uint32_t expectedPayloadLen = numChirps * (numRXPerMMIC * adcLen * 2 +		// 期望PayLoad长度计算
			CHIRP_HEADER_BYTES_CHEETAH + CHIRP_END_BYTES);

		/* ********* 读取Payload部分 ********* */
		if (isSkipFrm)
		{
			if (fseek(fid, payloadLen - CHIRP_HEADER_BYTES_CHEETAH, SEEK_CUR) != 0) {
				fprintf(stderr, "跳过 payload 失败\n");
				return -1;
			}
		}
		else
		{
			// 读取当前帧对应芯片的 payload（uint16 数组）
			uint16_t* payload = (uint16_t*)malloc(payloadWordCount * sizeof(uint16_t));
			if (payload == NULL) {
				fprintf(stderr, "payload 缓冲区分配失败\n");
				return -1;
			}
			if (fread(payload, sizeof(uint16_t), payloadWordCount, fid) != payloadWordCount) {
				fprintf(stderr, "读取 payload 数据不完整\n");
				free(payload);
				return -1;
			}

			// Payload 长度验证
			if (payloadLen != (uint32_t)expectedPayloadLen) {
				fprintf(stderr, "payload 长度不符合预期：%u != %d\n", payloadLen, expectedPayloadLen);
				free(payload);
				return -1;
			}

			// 存储 ADC 数据
			ctx->adcData.isSubframe1Exist = 1;
			size_t readerOffset = 0;
			for (int chirpIdx = 0; chirpIdx < numChirps; chirpIdx++) {
				size_t readerOffset_end = readerOffset + adcLen * numRXPerMMIC;
				// 提取每个接收通道的数据
				for (int rxIdx = 0; rxIdx < numRXPerMMIC; rxIdx++) {
					// 计算目标三维索引
					int targetRx = rxIdx + numRXPerMMIC * (tag - 13); // tag 13 对应第一个 MMIC
					if (targetRx >= numRxs) {
						fprintf(stderr, "目标接收通道索引越界\n");
						free(payload);
						return -1;
					}
					// 计算线性索引（列优先：adcLen × numChirps × numRxs）
					size_t baseIdx = targetRx * (adcLen * numChirps) + chirpIdx * adcLen;
					// 从 payload 中提取该通道的所有 adcLen 个样本（交错存储）
					for (int adcIdx = 0; adcIdx < adcLen; adcIdx++) {
						size_t payloadIdx = readerOffset + rxIdx + adcIdx * numRXPerMMIC;
						if (payloadIdx >= payloadWordCount) {
							fprintf(stderr, "payload 索引越界\n");
							free(payload);
							return -1;
						}
						ctx->adcData.sf1[baseIdx + adcIdx] = (float)payload[payloadIdx] - 2048.0f;
					}
				}
				// 更新 Offset，指向下一个 Chirp 的起始位置
				readerOffset = readerOffset_end + CHIRP_HEADER_BYTES_CHEETAH / 2;
			}

			free(payload);
		}

		// 更新读取子帧数量
		if (tag == (uint32_t)(13 + (numSubFrms - 1))) {
			cntSubFrame++;
		}

		// 跳过补零部分
		if (fseek(fid, packetAlignBytes + PACKET_PAD_ZERO_BYTES, SEEK_CUR) != 0) {
			fprintf(stderr, "跳过补零部分失败\n");
			return -1;
		}
	}

	/* ********** 参数赋值 ********** */
	if (!isSkipFrm) {
		
		if (IS_CRT) {
			// 有速度解模糊 (CRT)
			if (sfIdx <= numSubFrms) {
				ctx->wave_params.frame_class = 1;
			}
			else {
				ctx->wave_params.frame_class = 2;
			}
		}
		else {
			// 无速度解模糊 (CRT)
			ctx->wave_params.frame_class = 1;
		}

		// 最大不模糊速度计算
		ctx->wave_params.maxUmAmbVlc = ctx->wave_params.wavelength / 4.0f / ctx->wave_params.chirp_period;
	}

	return 0;
}
#endif

int extract_ADCData_from_buffer(GlbCtx* ctx, void* pFrameBuf)
 {

    // 临时变量
    int sfIdx = 0;
    int cntSubFrame = 0;
    int adcLen = ctx->wave_params.adcLen;
    int numChirps = ctx->wave_params.numChirps;
    int numRxs = ctx->basic_params.numRxs;
    int numRXPerMMIC = ctx->basic_params.numRXPerMMIC;
    int numSubFrms = ctx->wave_params.numSubFrms;

	(void)cntSubFrame;

    int dcnt = 0;
    uint16_t *pAdcSamp;
    uint32_t readerOff = 0;

    // 分配内存（如果尚未分配）
    size_t totalSamples = adcLen * numChirps * numRxs;
    if (ctx->adcData.sf1 == NULL) {
        ctx->adcData.sf1 = (float*)malloc(totalSamples * sizeof(float));
        if (ctx->adcData.sf1 == NULL) {
            fprintf(stderr, "内存分配失败\n");
            return -1;
        }
        // 初始化为零
        memset(ctx->adcData.sf1, 0, totalSamples * sizeof(float));
    }


	for (int chirpIdx = 0; chirpIdx < numChirps; chirpIdx++)
    {
		CheetahChirpHeadInfo hinfo;
        int chirpLen = sizeof(hinfo) + adcLen * sizeof(uint16_t) * numRXPerMMIC;
        readerOff = chirpLen * chirpIdx;
        memcpy(&hinfo, (char*)pFrameBuf + readerOff, sizeof(hinfo));
        readerOff += sizeof(hinfo);

		pAdcSamp = (uint16_t*) ((char*)pFrameBuf + readerOff);
        
		for (int rxIdx = 0; rxIdx < numRXPerMMIC; rxIdx++)
        {
			// 计算目标三维索引
			int targetRx = rxIdx + numRXPerMMIC * dcnt; // dcnt = 0： 对应第一个 MMIC
			if (targetRx >= numRxs)
			{
				fprintf(stderr, "目标接收通道索引越界\n");
				return -1;
			}

			// 计算线性索引（列优先：adcLen × numChirps × numRxs）
			size_t baseIdx = targetRx * (adcLen * numChirps) + chirpIdx * adcLen;
			// 提取该通道的所有 adcLen 个样本（交错存储）
            for (int adcIdx = 0; adcIdx < adcLen; adcIdx++)
            {
				size_t srcoff = adcIdx * numRXPerMMIC + rxIdx;
                ctx->adcData.sf1[baseIdx + adcIdx] = (float)(pAdcSamp[srcoff] >> 4) - 2048.0f;
			}
		}
	}

    ctx->adcData.isSubframe1Exist = 1;
    // 参数赋值
    if (IS_CRT) {
        // 有速度解模糊 (CRT)
        if (sfIdx <= numSubFrms) {
            ctx->wave_params.frame_class = 1;
        } else {
            ctx->wave_params.frame_class = 2;
        }
    } else {
        // 无速度解模糊 (CRT)
        ctx->wave_params.frame_class = 1;
    }

    // 最大不模糊速度计算
    ctx->wave_params.maxUmAmbVlc = ctx->wave_params.wavelength / 4.0f / ctx->wave_params.chirp_period;

    // ADC 数据检查（简化，仅检查子帧存在性）
    if (!ctx->adcData.isSubframe1Exist) {
        fprintf(stderr, "警告：数据帧不完整，缺少 subframe 1\n");
    }

    return 0;
}

