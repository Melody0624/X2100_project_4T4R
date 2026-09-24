/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */

#ifndef __MMC_SD_H__
#define __MMC_SD_H__

#include <stdint.h>

/*
 * SD   commands                              type  argument         response
 */
/* class 0 */
#define SD_SEND_RELATIVE_ADDR           3   /* bcr                      R6 */
#define SD_SEND_IF_COND                 8   /* bcr   [11:0]             R7 */
#define SD_SWITCH_VOLTAGE               11  /* ac                       R1 */

/* class 10 */
#define SD_SWITCH                       6   /* adtc   [31:0]            R1 */

/* class 5 */
#define SD_ERASE_WR_BLK_START           32  /* ac   [31:0] data addr    R1  */
#define SD_ERASE_WR_BLK_END             33  /* ac   [31:0] data addr    R1  */

/* Application commands */
#define SD_APP_SET_BUS_WIDTH            6   /* ac   [1:0] bus width     R1  */
#define SD_APP_SD_STATUS                13  /* adtc                     R1  */
#define SD_APP_SEND_NUM_WR_BLKS         22  /* adtc                     R1  */
#define SD_APP_OP_COND                  41  /* bcr  [31:0] OCR          R3  */
#define SD_APP_SEND_SCR                 51  /* adtc                     R1  */

/* OCR bit definitions */
#define SD_OCR_S18R                     (1 << 24)       /* 1.8V switching request */
#define SD_ROCR_S18A                    SD_OCR_S18R     /* 1.8V switching accepted by card */
#define SD_OCR_XPC                      (1 << 28)       /* SDXC power control */
#define SD_OCR_CCS                      (1 << 30)       /* Card Capacity Status */

/*
 * SD_SWITCH(CMD6) argument format
 *
 *      [31]    Check (0) ot switch (1)
 *      [30:24] Reserved (0)
 *      [23:20] Function group 6
 *      [19:16] Function group 5
 *      [15:12] Function group 4
 *      [11:8]  Function group 3
 *      [7:4]   Function group 2
 *      [3:0]   Function group 1
 */

/*
 * SD_SEND_IF_COND(CMD8) argument format
 *
 *      [31:12] Reserved (0)
 *      [11:8]  Host Voltage Supply Flags
 *      [7:0]   Check Pattern (0xAA)
 */

/*
 * SCR field definitions
 */

#define SCR_SPEC_VER_0                  0   /* Implements system specification 1.0 - 1.01 */
#define SCR_SPEC_VER_1                  1   /* Implements system specification 1.10 */
#define SCR_SPEC_VER_2                  2   /* Implements system specification 2.00-3.0X */

/*
 * SD bus widths
 */
#define SD_BUS_WIDTH_1                  0
#define SD_BUS_WIDTH_4                  2

#define SD_SCR_BUS_WIDTH_1              (1<<0)
#define SD_SCR_BUS_WIDTH_4              (1<<2)


/*
 * SD_SWITCH mode
 */
#define SD_SWITCH_CHECK                 0
#define SD_SWITCH_SET                   1

/*
 * SD_SWITCH function groups
 */
#define SD_SWITCH_GRP_ACCESS            0

/*
 * SD_SWITCH access modes
 */
#define SD_SWITCH_ACCESS_DEF            0
#define SD_SWITCH_ACCESS_HS             1


/*
 * SD支持通讯频率定义
 */
#define HIGH_SPEED_MAX_DTR              50000000
#define UHS_SDR104_MAX_DTR              208000000
#define UHS_SDR50_MAX_DTR               100000000
#define UHS_DDR50_MAX_DTR               50000000
#define UHS_SDR25_MAX_DTR               UHS_DDR50_MAX_DTR
#define UHS_SDR12_MAX_DTR               25000000

/*
 * SD 通讯模式定义
 */
#define UHS_SDR12_BUS_SPEED             0
#define HIGH_SPEED_BUS_SPEED            1
#define UHS_SDR25_BUS_SPEED             1
#define UHS_SDR50_BUS_SPEED             2
#define UHS_SDR104_BUS_SPEED            3
#define UHS_DDR50_BUS_SPEED             4

#define SD_MODE_HIGH_SPEED              (1 << HIGH_SPEED_BUS_SPEED)
#define SD_MODE_UHS_SDR12               (1 << UHS_SDR12_BUS_SPEED)
#define SD_MODE_UHS_SDR25               (1 << UHS_SDR25_BUS_SPEED)
#define SD_MODE_UHS_SDR50               (1 << UHS_SDR50_BUS_SPEED)
#define SD_MODE_UHS_SDR104              (1 << UHS_SDR104_BUS_SPEED)
#define SD_MODE_UHS_DDR50               (1 << UHS_DDR50_BUS_SPEED)

/*
 * SD 驱动能力定义
 */
#define SD_DRIVER_TYPE_B                0x01
#define SD_DRIVER_TYPE_A                0x02
#define SD_DRIVER_TYPE_C                0x04
#define SD_DRIVER_TYPE_D                0x08

/*
 * SD 电流限制
 */
#define SD_SET_CURRENT_LIMIT_200        0
#define SD_SET_CURRENT_LIMIT_400        1
#define SD_SET_CURRENT_LIMIT_600        2
#define SD_SET_CURRENT_LIMIT_800        3
#define SD_SET_CURRENT_NO_CHANGE        (-1)

#define SD_MAX_CURRENT_200              (1 << SD_SET_CURRENT_LIMIT_200)
#define SD_MAX_CURRENT_400              (1 << SD_SET_CURRENT_LIMIT_400)
#define SD_MAX_CURRENT_600              (1 << SD_SET_CURRENT_LIMIT_600)
#define SD_MAX_CURRENT_800              (1 << SD_SET_CURRENT_LIMIT_800)

#endif /* __MMC_SD_H__ */
