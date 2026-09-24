/**
 * @file mmw_msg_pkt.c
 * @brief Protocol Service implementation
 *
 * This file implements the protocol service for communication handling.
 *
 * @author Author
 * @date 2026-07-07
 *
 * Copyright (c) 2026 SenardMicro. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Change History:
 * - 2026-07-07: Initial version created.
 */

 #include "mmw_msg_pkt.h"
 #include "general_functions.h"
 #include "memory_pool.h"

Mmw_pkt_info* update_mmw_pkt_info_from_ctx(const GlbCtx *ctx, int pool_index)
{
	if (!ctx)
	{
		fprintf(stderr, "update_mmw_pkt_info_from_ctx: 上下文指针为空\n");
		return NULL;
	}

	Mmw_pkt_info *pktInfo = g_memoryPool[pool_index].mmw_pkt_info_buf;
	// 检查内存池是否已初始化
	if (!g_memoryPool[pool_index].initialized) {
		fprintf(stderr, "update_mmw_pkt_info_from_ctx: 内存池未初始化\n");
		return NULL;
	}

	memset(pktInfo, 0, sizeof(Mmw_pkt_info));

	pktInfo->detInfo.header.numDets = ctx->detInfo.numDets;
    for (int i = 0; i < pktInfo->detInfo.header.numDets; i ++)
    {
        pktInfo->detInfo.detObj[i].relRDIdx = ctx->detInfo.detObj[i].relRDIdx;
        pktInfo->detInfo.detObj[i].vlc = ctx->detInfo.detObj[i].vlc;
        pktInfo->detInfo.detObj[i].x_output = ctx->detInfo.detObj[i].x_output;
        pktInfo->detInfo.detObj[i].y_output = ctx->detInfo.detObj[i].y_output;
        pktInfo->detInfo.detObj[i].motion_state = ctx->detInfo.detObj[i].motion_state;

		pktInfo->detInfo.detObj[i].pwr = TO_UINT16_SCALE(ctx->detInfo.detObj[i].pwr, 100.0f);
		pktInfo->detInfo.detObj[i].snr = TO_UINT16_SCALE(ctx->detInfo.detObj[i].snr, 100.0f);
		pktInfo->detInfo.detObj[i].isPeak = ctx->detInfo.detObj[i].isPeak;
        pktInfo->detInfo.detObj[i].rng = ctx->detInfo.detObj[i].rng;
		pktInfo->detInfo.detObj[i].vlc_amb = TO_INT16_SCALE(ctx->detInfo.detObj[i].vlc_amb, 100.0f);
		pktInfo->detInfo.detObj[i].vlc_disAmb_conf = TO_UINT8_INT(ctx->detInfo.detObj[i].vlc_disAmb_conf);
		pktInfo->detInfo.detObj[i].vlc_disAmb_fac = TO_UINT8_INT(ctx->detInfo.detObj[i].vlc_disAmb_fac);
		pktInfo->detInfo.detObj[i].azm_deg = ctx->detInfo.detObj[i].azm_deg;
		pktInfo->detInfo.detObj[i].x_rcs = TO_INT16_SCALE(ctx->detInfo.detObj[i].x_rcs, 100.0f);
		pktInfo->detInfo.detObj[i].y_rcs = TO_INT16_SCALE(ctx->detInfo.detObj[i].y_rcs, 100.0f);
	}
    
    // 將有效的对象移到数组前面
    int valid_trk_count = 0;
    for (int i = 0; i < MAX_TRACKS; i ++)
    {
		// ctx->trkInfo.trkObj[i].isvalid = true;    //2026.03.26 临时处理，后续再判断是否有效
        if ((ctx->trkInfo.trkObj[i].isvalid) && (ctx->trkInfo.trkObj[i].track_state == TRACK_STATE_MATURE))
		// if (ctx->trkInfo.trkObj[i].isvalid)
		// if(1)
        {
#if NEW_PROTOCOL_ENABLE
            pktInfo->trkInfo.trkObj[valid_trk_count].isvalid = ctx->trkInfo.trkObj[i].isvalid;
            pktInfo->trkInfo.trkObj[valid_trk_count].trkID = ctx->trkInfo.trkObj[i].trkID;
            pktInfo->trkInfo.trkObj[valid_trk_count].motion_state = ctx->trkInfo.trkObj[i].motion_state;
            pktInfo->trkInfo.trkObj[valid_trk_count].maxLen_tcs = (uint8_t)TO_UINT8_INT(ctx->trkInfo.trkObj[i].maxLen_tcs*10);
            pktInfo->trkInfo.trkObj[valid_trk_count].maxWid_tcs = (uint8_t)TO_UINT8_INT(ctx->trkInfo.trkObj[i].maxWid_tcs*10);
            pktInfo->trkInfo.trkObj[valid_trk_count].x_output = (uint16_t)TO_UINT16_SCALE((ctx->trkInfo.trkObj[i].x_output + 255.0f), 8);
            pktInfo->trkInfo.trkObj[valid_trk_count].y_output = (uint16_t)TO_UINT16_SCALE((ctx->trkInfo.trkObj[i].y_output + 255.0f), 8);
            pktInfo->trkInfo.trkObj[valid_trk_count].vx_output = (uint16_t)TO_UINT16_SCALE((ctx->trkInfo.trkObj[i].vx_output + 102.0f), 20);
            pktInfo->trkInfo.trkObj[valid_trk_count].vy_output = (uint16_t)TO_UINT16_SCALE((ctx->trkInfo.trkObj[i].vy_output + 102.0f), 20);
			// pktInfo->trkInfo.trkObj[valid_trk_count].x_output = ctx->trkInfo.trkObj[i].x_rcs;
            // pktInfo->trkInfo.trkObj[valid_trk_count].y_output = ctx->trkInfo.trkObj[i].y_rcs;
            // pktInfo->trkInfo.trkObj[valid_trk_count].vx_output = ctx->trkInfo.trkObj[i].vx_rcs;
            // pktInfo->trkInfo.trkObj[valid_trk_count].vy_output = ctx->trkInfo.trkObj[i].vy_rcs;
            pktInfo->trkInfo.trkObj[valid_trk_count].heading_output = (uint16_t)TO_UINT16_SCALE((ctx->trkInfo.trkObj[i].heading_output + 180.0f), 2.5f);
            valid_trk_count ++;
#else 
            pktInfo->trkInfo.trkObj[valid_trk_count].isvalid = ctx->trkInfo.trkObj[i].isvalid;
            pktInfo->trkInfo.trkObj[valid_trk_count].trkID = ctx->trkInfo.trkObj[i].trkID;
            pktInfo->trkInfo.trkObj[valid_trk_count].motion_state = ctx->trkInfo.trkObj[i].motion_state;
            pktInfo->trkInfo.trkObj[valid_trk_count].maxLen_tcs = ctx->trkInfo.trkObj[i].maxLen_tcs;
            pktInfo->trkInfo.trkObj[valid_trk_count].maxWid_tcs = ctx->trkInfo.trkObj[i].maxWid_tcs;
            pktInfo->trkInfo.trkObj[valid_trk_count].x_output = ctx->trkInfo.trkObj[i].x_output;
            pktInfo->trkInfo.trkObj[valid_trk_count].y_output = ctx->trkInfo.trkObj[i].y_output;
            pktInfo->trkInfo.trkObj[valid_trk_count].vx_output = ctx->trkInfo.trkObj[i].vx_output;
            pktInfo->trkInfo.trkObj[valid_trk_count].vy_output = ctx->trkInfo.trkObj[i].vy_output;
			// pktInfo->trkInfo.trkObj[valid_trk_count].x_output = ctx->trkInfo.trkObj[i].x_rcs;
            // pktInfo->trkInfo.trkObj[valid_trk_count].y_output = ctx->trkInfo.trkObj[i].y_rcs;
            // pktInfo->trkInfo.trkObj[valid_trk_count].vx_output = ctx->trkInfo.trkObj[i].vx_rcs;
            // pktInfo->trkInfo.trkObj[valid_trk_count].vy_output = ctx->trkInfo.trkObj[i].vy_rcs;
            pktInfo->trkInfo.trkObj[valid_trk_count].heading_output = ctx->trkInfo.trkObj[i].heading_output;
            valid_trk_count ++;
#endif
        }
    }
    // 设置有效的航迹对象总数
    pktInfo->trkInfo.header.numTrks = valid_trk_count;

#if EGO_VLC_ENABLE
	// Convert ego velocity from float to int16_t with 2 decimal places precision
	pktInfo->egoVlcInfo.egoVelocity_mps = TO_INT16_SCALE(ctx->egoVehInfo.egoSpeed, 100.0f);
#endif

#if WARNING_ENABLE
	WarnResult *warnRet = (WarnResult *)&ctx->warnResult;
	pktInfo->warnInfo.warnResult = *warnRet;
#endif

	return pktInfo;
}

int32_t adc_packetize_results(char *adcInput, uint32_t adcSize, uint32_t frameID, char *outBuf, uint32_t outBufSize,
							  const void *extraTLVData, uint32_t extraTLVLen, uint32_t extraTLVType)
{
	Mmw_output_message_header header;
	Mmw_output_message_tl tl[5];
	uint32_t tlvIdx = 0;
	uint32_t packetLen = 0;
	uint32_t totalPacketSize = 0;
	char *currentPtr = NULL;

	if (!adcInput || !outBuf) {
		fprintf(stderr, "adc_packetize_results: adcInput或outBuf为空\n");
		return -1;
	}

	memset((void *)&header, 0, sizeof(Mmw_output_message_header));
	memset((void *)&tl, 0, sizeof(Mmw_output_message_tl) * 5);

	header.platform = 0x24;
	header.magicWord[0] = 0x0102;
	header.magicWord[1] = 0x0304;
	header.magicWord[2] = 0x0506;
	header.magicWord[3] = 0x0708;

	packetLen = sizeof(Mmw_output_message_header);

	/* ---- ADC TLV ---- */
	tl[tlvIdx].type = MMW_OUTPUT_MSG_ADC_FRAME;
	tl[tlvIdx].length = adcSize;
	packetLen += sizeof(Mmw_output_message_tl) + tl[tlvIdx].length;
	tlvIdx++;

	header.numTLVs = tlvIdx;
	header.totalPacketLen = packetLen;
	header.frameNumber = frameID;
	totalPacketSize = header.totalPacketLen;

	/* 检查缓冲区是否足够大 */
	if (totalPacketSize > outBufSize) {
		fprintf(stderr, "mmw_packetize_results: 缓冲区溢出风险! 需要 %u 字节，但只有 %u 字节\n", 
				(unsigned int)totalPacketSize, (unsigned int)outBufSize);
		return -2;
	}

	memset(outBuf, 0, totalPacketSize);
	currentPtr = outBuf;

	/* ---- 拷贝包头 ---- */
	memcpy(currentPtr, &header, sizeof(Mmw_output_message_header));
	currentPtr += sizeof(Mmw_output_message_header);

	tlvIdx = 0;
	/* ---- 拷贝ADC数据 ---- */
	memcpy(currentPtr, &tl[tlvIdx], sizeof(Mmw_output_message_tl));
	currentPtr += sizeof(Mmw_output_message_tl);
	memcpy(currentPtr, adcInput, adcSize);

	return (int32_t)totalPacketSize;
}

/**
 * @brief 將算法结果(Mmw_pkt_info)按TLV协议封包到缓冲区
 *        统一处理 DetInfo / TrkInfo / EgoVlcInfo / WarnInfo 及可选的额外TLV。
 * @return >0 封包总字节数；-1 pktInfo为空；
 */
int32_t mmw_packetize_results(Mmw_pkt_info *pktInfo, uint32_t frameID, char *outBuf,
							  const void *extraTLVData, uint32_t extraTLVLen, uint32_t extraTLVType)
{
	Mmw_output_message_header header;
	Mmw_output_message_tl tl[5];
	uint32_t tlvIdx = 0;
	uint32_t packetLen = 0;
	uint32_t totalPacketSize = 0;
	char *currentPtr = NULL;

	if (!pktInfo || !outBuf) {
		fprintf(stderr, "mmw_packetize_results: pktInfo或outBuf为空\n");
		return -1;
	}

	memset((void *)&header, 0, sizeof(Mmw_output_message_header));
	memset((void *)&tl, 0, sizeof(Mmw_output_message_tl) * 5);

	header.platform = 0x24;
	header.magicWord[0] = 0x0102;
	header.magicWord[1] = 0x0304;
	header.magicWord[2] = 0x0506;
	header.magicWord[3] = 0x0708;

	packetLen = sizeof(Mmw_output_message_header);

	/* ---- 可选额外TLV (如原始ADC数据) ---- */
	if (extraTLVData && (extraTLVLen > 0)) {
		tl[tlvIdx].type = extraTLVType;
		tl[tlvIdx].length = extraTLVLen;
		packetLen += sizeof(Mmw_output_message_tl) + extraTLVLen;
		tlvIdx++;
	}

	/* ---- 检测信息TLV ---- */
	tl[tlvIdx].type = MMW_OUTPUT_MSG_MOTORCYCLE_DETINFO;
	tl[tlvIdx].length = sizeof(DetInfo_Header) + sizeof(MotorCycle_DetObj) * pktInfo->detInfo.header.numDets;
	packetLen += sizeof(Mmw_output_message_tl) + tl[tlvIdx].length;
	tlvIdx++;

	/* ---- 航迹信息TLV ---- */
	tl[tlvIdx].type = MMW_OUTPUT_MSG_MOTORCYCLE_TRKINFO;
	tl[tlvIdx].length = sizeof(TrkInfo_Header) + sizeof(MotorCycle_TrkObj) * pktInfo->trkInfo.header.numTrks;
	packetLen += sizeof(Mmw_output_message_tl) + tl[tlvIdx].length;
	tlvIdx++;

#if EGO_VLC_ENABLE
	/* ---- 自车速信息TLV ---- */
	tl[tlvIdx].type = MMW_OUTPUT_MSG_MOTORCYCLE_EGOVLCINFO;
	tl[tlvIdx].length = sizeof(Mmw_output_message_EgoVlcInfo);
	packetLen += sizeof(Mmw_output_message_tl) + tl[tlvIdx].length;
	tlvIdx++;
#endif

#if WARNING_ENABLE
	/* ---- 预警信息TLV ---- */
	tl[tlvIdx].type = MMW_OUTPUT_MSG_MOTORCYCLE_WARNINFO;
	tl[tlvIdx].length = sizeof(Mmw_output_message_WarnInfo);
	packetLen += sizeof(Mmw_output_message_tl) + tl[tlvIdx].length;
	tlvIdx++;
#endif

	header.numTLVs = tlvIdx;
#ifdef MMWDEMO_OUTPUT_PADDING_BYTES
	/* Round up packet length to multiple of MMWDEMO_OUTPUT_MSG_SEGMENT_LEN */
	header.totalPacketLen = MMWDEMO_OUTPUT_MSG_SEGMENT_LEN * ((packetLen + (MMWDEMO_OUTPUT_MSG_SEGMENT_LEN - 1)) / MMWDEMO_OUTPUT_MSG_SEGMENT_LEN);
#else
	header.totalPacketLen = packetLen;
#endif
	header.frameNumber = frameID;

	totalPacketSize = header.totalPacketLen;

	memset(outBuf, 0, totalPacketSize);
	currentPtr = outBuf;

	/* ---- 拷贝包头 ---- */
	memcpy(currentPtr, &header, sizeof(Mmw_output_message_header));
	currentPtr += sizeof(Mmw_output_message_header);

	tlvIdx = 0;

	/* ---- 拷贝额外TLV数据 ---- */
	if (extraTLVData && extraTLVLen > 0) {
		memcpy(currentPtr, &tl[tlvIdx], sizeof(Mmw_output_message_tl));
		currentPtr += sizeof(Mmw_output_message_tl);
		memcpy(currentPtr, extraTLVData, extraTLVLen);
		currentPtr += extraTLVLen;
		tlvIdx++;
	}

	/* ---- 拷贝检测信息 ---- */
	memcpy(currentPtr, &tl[tlvIdx], sizeof(Mmw_output_message_tl));
	currentPtr += sizeof(Mmw_output_message_tl);
	memcpy(currentPtr, &pktInfo->detInfo.header, sizeof(DetInfo_Header));
	currentPtr += sizeof(DetInfo_Header);
	if (pktInfo->detInfo.header.numDets > 0) {
		memcpy(currentPtr, &pktInfo->detInfo.detObj[0], sizeof(MotorCycle_DetObj) * pktInfo->detInfo.header.numDets);
		currentPtr += sizeof(MotorCycle_DetObj) * pktInfo->detInfo.header.numDets;
	}

	/* ---- 拷贝航迹信息 ---- */
	tlvIdx++;
	memcpy(currentPtr, &tl[tlvIdx], sizeof(Mmw_output_message_tl));
	currentPtr += sizeof(Mmw_output_message_tl);
	memcpy(currentPtr, &pktInfo->trkInfo.header, sizeof(TrkInfo_Header));
	currentPtr += sizeof(TrkInfo_Header);
	if (pktInfo->trkInfo.header.numTrks > 0) {
		memcpy(currentPtr, &pktInfo->trkInfo.trkObj[0], sizeof(MotorCycle_TrkObj) * pktInfo->trkInfo.header.numTrks);
		currentPtr += sizeof(MotorCycle_TrkObj) * pktInfo->trkInfo.header.numTrks;
	}

#if EGO_VLC_ENABLE
	/* ---- 拷贝自车速信息 ---- */
	tlvIdx++;
	memcpy(currentPtr, &tl[tlvIdx], sizeof(Mmw_output_message_tl));
	currentPtr += sizeof(Mmw_output_message_tl);
	memcpy(currentPtr, &pktInfo->egoVlcInfo, sizeof(Mmw_output_message_EgoVlcInfo));
	currentPtr += sizeof(Mmw_output_message_EgoVlcInfo);
#endif

#if WARNING_ENABLE
	/* ---- 拷贝预警信息 ---- */
	tlvIdx++;
	memcpy(currentPtr, &tl[tlvIdx], sizeof(Mmw_output_message_tl));
	currentPtr += sizeof(Mmw_output_message_tl);
	memcpy(currentPtr, &pktInfo->warnInfo, sizeof(Mmw_output_message_WarnInfo));
	currentPtr += sizeof(Mmw_output_message_WarnInfo);
#endif

#ifdef MMWDEMO_OUTPUT_PADDING_BYTES
	{
		uint8_t padding[MMWDEMO_OUTPUT_MSG_SEGMENT_LEN];
		uint32_t numPaddingBytes;
		memset(padding, 0, MMWDEMO_OUTPUT_MSG_SEGMENT_LEN);
		numPaddingBytes = MMWDEMO_OUTPUT_MSG_SEGMENT_LEN - (packetLen & (MMWDEMO_OUTPUT_MSG_SEGMENT_LEN - 1));
		if (numPaddingBytes < MMWDEMO_OUTPUT_MSG_SEGMENT_LEN) {
			memcpy(currentPtr, padding, numPaddingBytes);
			currentPtr += numPaddingBytes;
		}
	}
#endif

	return (int32_t)totalPacketSize;
}