#ifndef __FRAMES_SAVE_H__
#define __FRAMES_SAVE_H__

#include "mmw_msg_pkt.h"

#define USE_SAVE_THREAD                             // 定义则使用异步存储线程
#define USE_SAVE_ANGLE                           1  // 定义则保存角度

#if USE_SAVE_ANGLE
void set_save_angle(int32_t angle);
#endif

int save_init(char *dump_fname);
void save_close(void);
int save_frame_pkt_update_raw(void *rawdata, uint32_t len, uint32_t frameID, int pool_index);
int save_frame_pkt_update(const char *pktBuf, int32_t pktSize, uint32_t frameID, int pool_index);
int read_frame_raw_data(const char *filename, uint32_t frameID, void *buffer, uint32_t buffer_size, off_t offset);
int read_Det_data(const char *filename, uint32_t frameID, void *buffer, uint32_t buffer_size);
int read_Det_data(const char *filename, uint32_t frameID, void *buffer, uint32_t buffer_size);
int parse_detinfo_payload(const uint8_t *payload, uint32_t payload_size, DetInfo *detInfo);

#endif // __FRAMES_SAVE_H__