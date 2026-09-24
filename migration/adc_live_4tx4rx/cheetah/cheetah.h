#ifndef CHEETAH_H
#define CHEETAH_H

/**********************************************************************************
 * Copyright: Copyright © 2026 SenardMicro All Rights Reserved. // 
 * @Author: : wangziqian ziqian.wang@senardmicro.com
 * @Date: : 2026-01-04 10:12:35
 * @LastEditors: : wangziqian ziqian.wang@senardmicro.com
 * @LastEditTime: : 2026-01-04 19:59:46
 * @Description: : ${file_description}
 * Version: V1.0.0
 * Changelog: - v1.0.0 ${create_time} 初始版本
 * 系统名称-V1.0
**********************************************************************************/
#include <stdint.h>

#define SPI_BUG				0
#define BUF_SIZE			4096

#define CHEETAH_CHECK 		1

#define CMD_READ     		0xc
#define CMD_WRITE    		0x9
#define CMD_SINGLE   		0x1
#define CMD_BURST   		0x2
#define CMD_NOCHECK  		0x1
#define CMD_CHECK    		0x2
#define CMD_ADDRACK  		0xa3
 #define CMD_ADDRNACK 		0xac
#define CMD_DATAACK  		0xa5
#define CMD_DATANACK 		0xaa

#define SPI_READ_SINGLE_CHECK ((CMD_READ << 4) | (CMD_SINGLE << 2) | CMD_CHECK)
#define SPI_READ_SINGLE_NOCHECK ((CMD_READ << 4) | (CMD_SINGLE << 2) | CMD_NOCHECK)
#define SPI_READ_BURST_CHECK ((CMD_READ << 4) | (CMD_BURST << 2) | CMD_CHECK)
#define SPI_READ_BURST_NOCHECK ((CMD_READ << 4) | (CMD_BURST << 2) | CMD_NOCHECK)
#define SPI_WRITE_SINGLE_CHECK ((CMD_WRITE << 4) | (CMD_SINGLE << 2) | CMD_CHECK)
#define SPI_WRITE_SINGLE_NOCHECK ((CMD_WRITE << 4) | (CMD_SINGLE << 2) | CMD_NOCHECK)
#define SPI_WRITE_BURST_CHECK ((CMD_WRITE << 4) | (CMD_BURST << 2) | CMD_CHECK)
#define SPI_WRITE_BURST_NOCHECK ((CMD_WRITE << 4) | (CMD_BURST << 2) | CMD_NOCHECK)

#define CMD_DELAY                   -1
#define CMD_RESET_RAMS              -2
#define CMD_RADAR_START             -3
#define CMD_SET_MAX_FRAME_CNT       -4
#define CMD_SEND_CONFIGINFO         -5
#define CMD_GROUP_START             -6
#define CMD_GROUP_END               -7

struct reg_line
{
    uint16_t chipIdx;
    int16_t addr;
    uint16_t valLen;
    uint32_t value[64];
};



int cheetah_spi_init(void);
int cheetah_reg_read_single(int devidx, uint16_t addr, uint32_t *pdata, int check);
int cheetah_reg_write_single(int devidx, uint16_t addr, uint32_t val, int check);
int cheetah_reg_read_burst(int devidx, uint16_t addr, uint32_t *pdata, uint16_t cnt, int check);
int cheetah_reg_write_burst(int devidx, uint16_t addr, uint32_t *pdata, uint16_t cnt, int check);
void cheetah_start(void);
void cheetah_stop(void);
void cheetah_reset(void);
void reset_ram(int devidx);
void set_regs_to_target(struct reg_line *regscfg, int count);

#endif /* CHEETAH_H */
