/**
 * @file        parse_dat_detInfo.c
 * @brief       TLV 格式 .dat 文件检测信息解析实现。解析 TLV 消息头、Magic Word 校验、
 *              遍历 TLV Tag，提取检测点信息(DETINFO, Tag=21)并填充至 DetInfo 结构体。
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#include "parse_dat_detInfo.h"
#include <string.h>     // memset

#if (DOT_REPLAY == 1)
#include "frames_save.h"
#include "parse_dat_detInfo.h"
extern int DetFrameNum;
#endif

/*=============================================================================
 *  内部辅助: 从文件读取指定数量的 uint32
 *============================================================================*/
static int _read_uint32s(FILE* fid, uint32_t* buf, int count)
{
    size_t nread = fread(buf, sizeof(uint32_t), (size_t)count, fid);
    if (nread == 0) {
        return -1;  // EOF
    }
    if ((int)nread != count) {
        return -2;  // 读取不完整
    }
    return 0;
}

/*=============================================================================
 *  内部辅助: 跳过一帧中所有 TLV 的 payload (仅移动文件指针, 不解析)
 *
 *  在读取 msg_header 之后调用, 已从 fid 中读取了 MSG_HEADER_NUM_UINT32 个 uint32.
 *  numTLVs 从 msg_header[6] 获取.
 *============================================================================*/
static int _skip_all_TLVs(FILE* fid, uint32_t numTLVs)
{
    for (uint32_t tlvIdx = 0; tlvIdx < numTLVs; tlvIdx++) {
        uint32_t tag;
        uint32_t payload_length;

        if (_read_uint32s(fid, &tag, 1) != 0) return -2;
        if (_read_uint32s(fid, &payload_length, 1) != 0) return -2;

        if (payload_length > 0) {
            if (fseek(fid, (long)payload_length, SEEK_CUR) != 0) return -2;
        }
    }
    return 0;
}

/*=============================================================================
 *  内部辅助: 解析 DETINFO payload (TLV Tag = 21)
 *
 *  二进制布局 (源自 MATLAB parse_x2000_tlv_v2.m):
 *    [numDets]       uint32
 *    [numStaticDets] uint32
 *    对每个检测点 (共 15 个字段, 各 4 字节):
 *      1. relRDIdx         uint32  -> int
 *      2. vlc              float
 *      3. x_output         float
 *      4. y_output         float
 *      5. motion_state     uint32  -> int
 *      6. pwr              float
 *      7. snr              float
 *      8. isPeak           uint32  -> bool
 *      9. rng              float
 *     10. vlc_amb          float
 *     11. vlc_disAmb_conf  float   -> int
 *     12. vlc_disAmb_fac   float   -> int
 *     13. azm_deg          float
 *     14. x_rcs            float
 *     15. y_rcs            float
 *============================================================================*/
static int _parse_detInfo_payload(FILE* fid, DetInfo* detInfo)
{
	fread(&detInfo->numDets, sizeof(uint16_t), 1, fid);

	if (detInfo->numDets > MAX_DETECTIONS)
	{
		fprintf(stderr, "Error: numDets (%d) exceeds MAX_DETECTIONS (%d)\n", detInfo->numDets, MAX_DETECTIONS);
		return -1;
	}
		
	// 逐检测点解析
	for (int i = 0; i < detInfo->numDets; i++)
	{
		DetObj* det = &detInfo->detObj[i];
		fread(&det->relRDIdx, sizeof(uint32_t), 1, fid);
		fread(&det->vlc, sizeof(float), 1, fid);
		fread(&det->x_output, sizeof(float), 1, fid);
		fread(&det->y_output, sizeof(float), 1, fid);
		fread(&det->motion_state, sizeof(uint32_t), 1, fid);
		fread(&det->pwr, sizeof(float), 1, fid);
		fread(&det->snr, sizeof(float), 1, fid);
		fread(&det->isPeak, sizeof(uint32_t), 1, fid);
		fread(&det->rng, sizeof(float), 1, fid);
		fread(&det->vlc_amb, sizeof(float), 1, fid);
		fread(&det->vlc_disAmb_conf, sizeof(float), 1, fid);
		fread(&det->vlc_disAmb_fac, sizeof(float), 1, fid);
		fread(&det->azm_deg, sizeof(float), 1, fid);
		fread(&det->x_rcs, sizeof(float), 1, fid);
		fread(&det->y_rcs, sizeof(float), 1, fid);
	}
	return 0;
}

/*=============================================================================
 *  公共接口: 解析一帧 TLV 数据, 仅提取 DETINFO
 *
 *  帧格式 (源自 MATLAB parse_x2000_tlv_v2.m):
 *    [msg_header]  9 x uint32 (36 字节)
 *      [0]: magic word 低位  (0x03040102)
 *      [1]: magic word 高位  (0x07080506)
 *      [3]: totalPacketLen
 *      [5]: frameNumber
 *      [6]: numTLVs
 *    对每个 TLV:
 *      [tag]            uint32
 *      [payload_length] uint32
 *      [payload]        tag 决定其内容
 *
 *  跳帧逻辑 (参考 get_adc_from_dat_file.c):
 *    - 当 isParseDatDetInfo == false 时, 只读取 msg_header 并跳过所有 TLV
 *      payload, 不解析任何内容, 仅移动文件指针.
 *============================================================================*/
int parse_dat_detInfo(FILE* fid, GlbCtx* ctx, bool isSkipFrm)
{
	if (!fid || !ctx)
		return -1;

	// -------------------- 逐字节搜索 Magic Word --------------------
	static const uint8_t magic_bytes[8] = {
		0x02, 0x01, 0x04, 0x03,    // MAGIC_WORD_0 = 0x03040102 (小端)
		0x06, 0x05, 0x08, 0x07     // MAGIC_WORD_1 = 0x07080506 (小端)
	};

	uint8_t search_buf[MAGIC_BYTE_SEARCH_LIMIT + 8];
	int     byte_cnt = 0;
	bool    isFndMagic = false;

	while (!isFndMagic) {
		uint8_t byte;
		if (fread(&byte, 1, 1, fid) != 1) {
			return -1;  // EOF, 正常结束
		}
		search_buf[byte_cnt] = byte;
		byte_cnt++;

		// 检查缓冲区尾部是否匹配 8 字节 magic word
		if (byte_cnt >= 8 &&
			memcmp(&search_buf[byte_cnt - 8], magic_bytes, 8) == 0) {
			isFndMagic = true;
			break;
		}

		// 超过 500 字节搜索限制, 视为解析结束 (MATLAB: isEndOfParse = true)
		if (byte_cnt > MAGIC_BYTE_SEARCH_LIMIT) {
			return -1;
		}
	}

	// ---- 2. 读取剩余消息头 (msg_header[2..8], 共 7 个 uint32) ----
	uint32_t msg_header_tail[5];
	int ret = _read_uint32s(fid, msg_header_tail, 5);
	if (ret != 0) {
		return -1;  // EOF 或 读取不完整，均视为文件结束 (避免后续帧因数据不完整而反复报错)
	}

	// 拼接完整消息头 (msg_header[0..1] 已知为 magic word)
	uint32_t msg_header[MSG_HEADER_NUM_UINT32];
	msg_header[0] = MAGIC_WORD_0;
	msg_header[1] = MAGIC_WORD_1;
	memcpy(&msg_header[2], msg_header_tail, 5 * sizeof(uint32_t));

	// ---- 3. 获取帧元信息 ----
	uint32_t frameNumber = msg_header[5];
	uint32_t numTLVs = msg_header[6];
	
#if (DOT_REPLAY == 1)
	DetFrameNum = frameNumber;
#endif
	// 更新帧 ID
	// ctx->frmInfo.lastFrmID = ctx->frmInfo.frmID;
	// ctx->frmInfo.frmID = (int)frameNumber;

	// ---- 5. 解析模式: 遍历所有 TLV, 仅提取 DETINFO ----
	for (uint32_t tlvIdx = 0; tlvIdx < numTLVs; tlvIdx++) {
		uint32_t tag;
		uint32_t payload_length;

		if (_read_uint32s(fid, &tag, 1) != 0)
			return -2;
		if (_read_uint32s(fid, &payload_length, 1) != 0)
			return -2;
		if (payload_length == 0)
		{
			printf("payload长度为零，跳过帧\n");
			return -2;
		}

		// Tag 合法性检查
		if (tag < 21 || tag > 24) {
			// 未知 Tag, 跳过 payload
			if (fseek(fid, (long)payload_length, SEEK_CUR) != 0)
				return -2;
			continue;
		}

		if (isSkipFrm)
		{
			if (fseek(fid, (long)payload_length, SEEK_CUR) != 0)
				return -2;
			else
				continue;
		}

		switch (tag) {
		case TLV_TAG_DETINFO:
			// 解析检测点信息
			if (_parse_detInfo_payload(fid, &ctx->detInfo) != 0)
				return -1;
			break;
		case TLV_TAG_TRACKINFO:
			if (fseek(fid, (long)payload_length, SEEK_CUR) != 0) return -2;
			break;
		case TLV_TAG_EGO_VLC: {
			/* 韌體記錄的 ego 速度。開啟 RANSAC 估計時不可寫入 egoVehInfo.egoSpeed：
			   該欄位兼作 getEgoVlcByStatic() 的 EMA 平滑狀態（上一幀平滑值），
			   逐幀覆蓋會污染遞迴、使回放偏離韌體上的純 EMA 行為 */
			float egoSpeed_fw;
			fread(&egoSpeed_fw, sizeof(float), 1, fid);
#if !EGO_VLC_EST_ENABLE
			ctx->egoVehInfo.egoSpeed = egoSpeed_fw;
#else
			(void)egoSpeed_fw;
#endif
			break;
		}
		case TLV_TAG_WARNINFO:
			if (fseek(fid, (long)payload_length, SEEK_CUR) != 0) return -2;
			break;
		default:
			// 非 DETINFO 的 TLV, 跳过 payload
			if (fseek(fid, (long)payload_length, SEEK_CUR) != 0) return -2;
			break;
		}
	}
	return 0;
}