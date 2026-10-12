#ifndef EOL_CALIBRATION_H
#define EOL_CALIBRATION_H

#include <stdint.h>
#include <stdbool.h>
#include "../motor_cycle_demo/inc/radar_types.h"

// Configuration Macros
#define EOL_CALIB_MATCH_REQUIRED  2
#define EOL_CALIB_MATCH_WINDOW    3
#define EOL_CALIB_TIMEOUT_FRAMES  9 // 1 seconds at 4fps

// Tolerances
#define EOL_CALIB_TOL_DIST 0.5f // meters
#define EOL_CALIB_TOL_VEL  0.2f // m/s
#define EOL_CALIB_TOL_ANG  7.0f // degrees

extern uint8_t g_calib_operation;
extern uint8_t g_calib_result_status;

// UDS Data Identifiers
#define DID_BOOTLOADER_VERSION    0xF180
#define DID_APP_SOFTWARE_VERSION  0xF195
#define DID_SERIAL_NUMBER         0x0309
#define DID_PRODUCTION_DATE       0xF199
#define DID_CALIB_TARGET_DIST     0x0100
#define DID_CALIB_TARGET_ANG      0x0101
#define DID_CALIB_POWER_TH        0x0102
#define DID_CALIB_TARGET_VEL      0x0103
#define DID_CALIB_OPERATION       0x0110
#define DID_CALIB_RESULT_ANG      0x0200
#define DID_CALIB_RESULT_STATUS   0x0201

// Initial Phase Calibration (CAN) Data Identifiers
#define DID_PHASE_CALIB_TARGET_PARA    0x0130  // 设置标靶信息: minRange(1B), minAng(1B), maxAng(1B)
#define DID_PHASE_CALIB_SET_ANGLE      0x0133  // 设置当前转台角度: 2bytes, 666=无效, -90~90有效
#define DID_PHASE_CALIB_START          0x0135  // 启动校准: 0=停止, 1=启动

// CAN response status codes
#define PHASE_CALIB_CAN_STATUS_SUCCESS 0x01
#define PHASE_CALIB_CAN_STATUS_FAIL    0x00

void eol_calib_start(void);
void eol_calib_stop(void);
void eol_calib_process_frame(DetInfo* detInfo);

int eol_uds_process_request(const uint8_t *req_data, uint8_t req_len, 
                            uint8_t *resp_data, uint8_t *resp_len);

#endif
