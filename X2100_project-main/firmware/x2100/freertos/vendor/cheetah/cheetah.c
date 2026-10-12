/**********************************************************************************
 * Copyright: Copyright © 2026 SenardMicro All Rights Reserved. // 
 * @Author: : wangziqian ziqian.wang@senardmicro.com
 * @Date: : 2026-01-04 10:12:51
 * @LastEditors: : wangziqian ziqian.wang@senardmicro.com
 * @LastEditTime: : 2026-01-05 16:20:34
 * @Description: : ${file_description}
 * Version: V1.0.0
 * Changelog: - v1.0.0 ${create_time} 初始版本
 * 系统名称-V1.0
**********************************************************************************/

#include <delay.h>
#include <driver/spi.h>
#include <driver/gpio.h>


#include "cheetah.h"

uint8_t wbuf[BUF_SIZE];
uint8_t rbuf[BUF_SIZE];

struct spi_config_data config = {
    .id = 1,
    .cs_pin = GPIO_PD(22),              /* 指定 GPIO 作为 CS 引脚 */
    .clk_rate = 2 * 1000 * 1000,         /* 配置时钟频率 */
    .cs_valid_level = Spi_valid_low,   /* 配置spi有效电平为高 */
    .tx_endian = Spi_endian_msb_first,
    .rx_endian = Spi_endian_msb_first,
    .bits_per_word = 8,     /* 数据位宽 8,即传输过程中的最小数据单位为8bit */
    .spi_pha = 0,
    .spi_pol = 0,
    .loop_mode = 0,
};

struct spi_device *spi;

int cheetah_spi_init(void)
{
    spi = spi_register(&config);
    
    return 0;
}

int cheetah_reg_read_single(int devidx, uint16_t addr, uint32_t *pdata, int check)
{

    int ret = 0;
    int len;
    if (pdata == NULL)
    {
        printf("%s, invalid param pdata\r\n", __FUNCTION__);
        return -1;
    }
    
    memset(wbuf, 0, sizeof(wbuf));
    memset(rbuf, 0, sizeof(rbuf));



    if (check) {
        wbuf[0] = SPI_READ_SINGLE_CHECK;
        len = 10;
    } else {
        wbuf[0] = SPI_READ_SINGLE_NOCHECK;
        len = 8;
    }

    wbuf[1] = (addr >> 8) & 0xff;
    wbuf[2] = addr & 0xff;

    if (check) {
        wbuf[3] = wbuf[1] + wbuf[2];
    } else {
        wbuf[3] = 0;
    }

    struct spi_message msg = {
        .tx_buf = wbuf,  /* tx_buf可以为空,表示只接收不发送 */
        .rx_buf = rbuf,  /* rx_buf可以为空,表示只发送不接收 */
        .tlen  = len,      /* tlen 可以为 0,表示只接收不发送 */
        .rlen  = len,      /* rlen 可以为 0,表示只发送不接收 */
        .cs_change = 1,     /* 第一个transfer结束改变 cs 电平, 0 表示传输结束不改变 */
    };

    // ret = spi_transfer(&spidev[devidx], wbuf, rbuf, len);
	// ret = spi_transfer(devidx, wbuf, rbuf, len);
    spi_transfer(spi, &msg, 1);

    *pdata = ((rbuf[4] << 24) | (rbuf[5] << 16) | (rbuf[6] << 8) | rbuf[7]);
	if (SPI_BUG) //test
	{
		printf("read addr = %x  val = 0x%x\r\n", addr, *pdata);
	}

    if (check) {
        if (rbuf[8] != CMD_ADDRACK) {
            printf("spi_read_single: addrAck error\n");
            ret = -1;
        }
        uint8_t data_cks = rbuf[4] + rbuf[5] + rbuf[6] + rbuf[7];
        if (data_cks != rbuf[9]) {
            printf("spi_read_single: dataCks error\n");
            ret = -1;
        }
    }

	return ret;
}

int cheetah_reg_write_single(int devidx, uint16_t addr, uint32_t val, int check)
{
    int len;
    int ret = 0;

    memset(wbuf, 0, sizeof(wbuf));
    memset(rbuf, 0, sizeof(rbuf));

	if (check) {
		wbuf[0] = SPI_WRITE_SINGLE_CHECK;
		len = 10;
	} else {
		wbuf[0] = SPI_WRITE_SINGLE_NOCHECK;
		len = 9;
	}

	if (SPI_BUG) //test
	{
		// printf("write addr = %x  val = 0x%x\r\n", addr,val);
	}
	
	wbuf[1] = (addr >> 8) & 0xff;
	wbuf[2] = addr & 0xff;

	if (check) {
		wbuf[3] = wbuf[1] + wbuf[2];
	} else {
		wbuf[3] = 0;
	}

	wbuf[4] = (val >> 24) & 0xff;
	wbuf[5] = (val >> 16) & 0xff;
	wbuf[6] = (val >> 8) & 0xff;
	wbuf[7] = val & 0xff;

	if (check) {
		wbuf[8] = wbuf[4] + wbuf[5] + wbuf[6] + wbuf[7];
	} else {
		wbuf[8] = 0;
	}
	// wbuf[9]; // tail_dummy

    struct spi_message msg = {
        .tx_buf = wbuf,  /* tx_buf可以为空,表示只接收不发送 */
        .rx_buf = rbuf,  /* rx_buf可以为空,表示只发送不接收 */
        .tlen  = len,      /* tlen 可以为 0,表示只接收不发送 */
        .rlen  = len,      /* rlen 可以为 0,表示只发送不接收 */
        .cs_change = 1,     /* 第一个transfer结束改变 cs 电平, 0 表示传输结束不改变 */
    };
    // spi_transfer(&spidev[devidx], wbuf, rbuf, len);
	// spi_transfer(devidx, wbuf, rbuf, len);
    spi_transfer(spi, &msg, 1);

	if (check) {
		if (rbuf[8] != CMD_ADDRACK) {
			printf("spi_write_single: addrAck error\n");
			ret = -1;
		}

		if (rbuf[9] != CMD_DATAACK) {
			printf("spi_write_single: dataAck error\n");
			ret = -1;
		}
	}

	return ret;

}

int cheetah_reg_read_burst(int devidx, uint16_t addr, uint32_t *pdata, uint16_t cnt, int check)
{
    int len;
    int ret = 0;

    if (pdata == NULL)
    {
        printf("%s, invalid param pdata\r\n", __FUNCTION__);
        return -1;
    }

    memset(wbuf, 0, sizeof(wbuf));
    memset(rbuf, 0, sizeof(rbuf));

    if (check) {
        wbuf[0] = SPI_READ_BURST_CHECK;
        len = 1 + 2 + 2 + 1 + 4 * cnt + 2;
    } else {
        wbuf[0] = SPI_READ_BURST_NOCHECK;
        len = 1 + 2 + 2 + 1 + 4 * cnt;
    }

		wbuf[1] = (addr >> 8) & 0xff;
		wbuf[2] = addr & 0xff;
		wbuf[3] = (cnt >> 8) & 0xff;
		wbuf[4] = cnt & 0xff;

		if (check) {
			wbuf[5] = wbuf[1] + wbuf[2] + wbuf[3] + wbuf[4];
		} else {
			wbuf[5] = 0;
		}

        struct spi_message msg = {
            .tx_buf = wbuf,  /* tx_buf可以为空,表示只接收不发送 */
            .rx_buf = rbuf,  /* rx_buf可以为空,表示只发送不接收 */
            .tlen  = len,      /* tlen 可以为 0,表示只接收不发送 */
            .rlen  = len,      /* rlen 可以为 0,表示只发送不接收 */
            .cs_change = 1,     /* 第一个transfer结束改变 cs 电平, 0 表示传输结束不改变 */
        };
        // spi_transfer(&spidev[devidx], wbuf, rbuf, len);
		// spi_transfer(devidx, wbuf, rbuf, len);
        spi_transfer(spi, &msg, 1);

		uint8_t data_cks = 0;
		for (int i = 0; i < cnt; i++) {
			uint8_t d[4];
			d[0] = rbuf[6 + 4 * i + 0];
			d[1] = rbuf[6 + 4 * i + 1];
			d[2] = rbuf[6 + 4 * i + 2];
			d[3] = rbuf[6 + 4 * i + 3];

			pdata[i] = ((d[0] << 24) | (d[1] << 16) | (d[2] << 8) |
				  d[3]);

			data_cks += d[0] + d[1] + d[2] + d[3];
		}

		if (SPI_BUG) //test
		{
			printf("read addr = 0x%x  val = ", addr);
			for (size_t i = 0; i < cnt; i++)
			{
				printf("0x%x, ", pdata[i]);
			}
			printf("\r\n");
		}

		if (check) {
			if (rbuf[6 + cnt * 4] != CMD_ADDRACK) {
				printf("spi_read_burst1: addrAck error\n");
				ret = -1;
			}
			if (data_cks != rbuf[6 + cnt * 4 + 1]) {
				printf("spi_read_burst2: dataCks error\n");
				ret = -1;
			}
		}

	return ret;
}

int cheetah_reg_write_burst(int devidx, uint16_t addr, uint32_t *pdata, uint16_t cnt, int check)
{
    int len = 0;
    int ret = 0;
    
    if (pdata == NULL)
    {
        printf("%s, invalid param pdata\r\n", __FUNCTION__);
        return -1;
    }

    memset(wbuf, 0, sizeof(wbuf));
    memset(rbuf, 0, sizeof(rbuf));

    if (check) {
        wbuf[0] = SPI_WRITE_BURST_CHECK;
        len = 1 + 2 + 2 + 1 + 4 * (int)cnt + 2;
    } else {
        wbuf[0] = SPI_WRITE_BURST_NOCHECK;
        len = 1 + 2 + 2 + 1 + 4 * (int)cnt + 1;
    }

	// if (SPI_BUG) //test
	// {
	// 	printf("write addr = 0x%x len = %d val = ", addr, cnt);
	// 	for (size_t i = 0; i < cnt; i++)
	// 	{
	// 		printf("0x%x, ", pdata[i]);
	// 	}
	// 	printf("\r\n");
	// }

    wbuf[1] = (addr >> 8) & 0xff;
    wbuf[2] = addr & 0xff;
    wbuf[3] = (cnt >> 8) & 0xff;
    wbuf[4] = cnt & 0xff;

    if (check) {
        wbuf[5] = wbuf[1] + wbuf[2] + wbuf[3] + wbuf[4];
    } else {
        wbuf[5] = 0;
    }

	uint8_t data_cks = 0;

    for (int i = 0; i < cnt; i++) {
        wbuf[6 + 4 * i + 0] = (pdata[i] >> 24) & 0xff;
        wbuf[6 + 4 * i + 1] = (pdata[i] >> 16) & 0xff;
        wbuf[6 + 4 * i + 2] = (pdata[i] >> 8) & 0xff;
        wbuf[6 + 4 * i + 3] = pdata[i] & 0xff;
        data_cks += wbuf[6 + 4 * i + 0] + wbuf[6 + 4 * i + 1] +
                wbuf[6 + 4 * i + 2] + wbuf[6 + 4 * i + 3];
    }

    wbuf[6 + cnt * 4] = data_cks;
    wbuf[6 + cnt * 4 + 1] = 0;

    struct spi_message msg = {
        .tx_buf = wbuf,  /* tx_buf可以为空,表示只接收不发送 */
        .rx_buf = rbuf,  /* rx_buf可以为空,表示只发送不接收 */
        .tlen  = len,      /* tlen 可以为 0,表示只接收不发送 */
        .rlen  = len,      /* rlen 可以为 0,表示只发送不接收 */
        .cs_change = 1,     /* 第一个transfer结束改变 cs 电平, 0 表示传输结束不改变 */
    };
    // spi_transfer(&spidev[devidx], wbuf, rbuf, len);
    // spi_transfer(devidx, wbuf, rbuf, len);
    spi_transfer(spi, &msg, 1);

    if (check) {
        if (rbuf[6 + cnt * 4] != CMD_ADDRACK) {
            printf("spi_write_burst1: addrAck error\n");
            ret = -1;
        }
        if (CMD_DATAACK != rbuf[6 + cnt * 4 + 1]) {
            printf("spi_write_burst2: dataAck error\n");
            ret = -1;
        }
    }
	
	return ret;
}




void cheetah_start(void)
{
    cheetah_reg_write_single(0, 0x1053, 0x10000000, CHEETAH_CHECK);
}

int cheetah_start_checked(void)
{
    const uint32_t frame_en_mask = 0x10000000;
    const uint32_t dd_mode_mask = 0x00000018;
    const uint32_t system_ready_mask = 0x00000004;
    const uint32_t runtime_error_mask = 0x00020040;
    const uint32_t fsync_mask = 0x00000002;
    const uint32_t status_clear_mask = runtime_error_mask | fsync_mask;
    uint32_t value = 0;
    uint32_t int_status = 0;
    int fsync_seen = 0;
    int ret;

    /*
     * 0x1053[4:3] is the read-only RO_DD_MODE state and bit 2 is
     * RO_SYSTEM_RDY.  A healthy stage reaches 0x00000004 before FRAME_EN is
     * armed.  A non-zero RO_DD_MODE observed here has correlated with
     * RO_INT_STATUS bit 17/6 and no MIPI frame, so do not start capture until
     * the stage configuration has returned to ready/idle.
     */
    for (int poll = 0; poll < 100; poll++) {
        ret = cheetah_reg_read_single(0, 0x1053, &value, CHEETAH_CHECK);
        if (ret == 0 && (value & frame_en_mask) == 0 &&
            (value & dd_mode_mask) == 0 &&
            (value & system_ready_mask) != 0)
            break;

        if (poll == 0) {
            printf("[PhaseCalib] Waiting for Cheetah ready before data-flow "
                   "start: 0x1053=0x%08x, ret=%d\n", value, ret);
        }
        mdelay(10);
    }
    if (ret != 0 || (value & frame_en_mask) != 0 ||
        (value & dd_mode_mask) != 0 ||
        (value & system_ready_mask) == 0) {
        printf("[PhaseCalib] Cheetah did not become ready before data-flow "
               "start: 0x1053=0x%08x, ret=%d\n", value, ret);
        return ret != 0 ? ret : -1;
    }

    /*
     * 0x10D2 is sticky.  Clear the two observed runtime errors and the FSync
     * notification before arming FRAME_EN, then restore 0x10D1 to its
     * documented maintain state.  A newly asserted FSync bit then proves
     * that this start, rather than an earlier frame, reached the frame-sync
     * event.  FSync does not prove that CSI transmitted a complete frame;
     * camera_wait_frame() remains the authoritative completion check.
     */
    ret = cheetah_reg_write_single(0, 0x10D1, ~status_clear_mask,
                                   CHEETAH_CHECK);
    if (ret == 0)
        ret = cheetah_reg_write_single(0, 0x10D1, 0xFFFFFFFF,
                                       CHEETAH_CHECK);
    if (ret == 0)
        ret = cheetah_reg_read_single(0, 0x10D2, &int_status,
                                      CHEETAH_CHECK);
    if (ret != 0 || (int_status & status_clear_mask) != 0) {
        printf("[PhaseCalib] Failed to clear Cheetah runtime status before "
               "data-flow start: 0x10D2=0x%08x, ret=%d\n",
               int_status, ret);
        return ret != 0 ? ret : -1;
    }

    for (int attempt = 1; attempt <= 2; attempt++) {
        fsync_seen = 0;
        ret = cheetah_reg_write_single(0, 0x1053, 0x10000000,
                                       CHEETAH_CHECK);
        if (ret == 0) {
            for (int poll = 0; poll < 30; poll++) {
                ret = cheetah_reg_read_single(0, 0x1053, &value,
                                              CHEETAH_CHECK);
                if (ret == 0)
                    ret = cheetah_reg_read_single(0, 0x10D2, &int_status,
                                                  CHEETAH_CHECK);
                if (ret != 0 || (value & frame_en_mask) == 0 ||
                    (value & dd_mode_mask) != 0 ||
                    (value & system_ready_mask) == 0 ||
                    (int_status & runtime_error_mask) != 0)
                    break;
                if ((int_status & fsync_mask) != 0) {
                    fsync_seen = 1;
                    break;
                }
                mdelay(10);
            }
        }
        if (ret == 0 && (value & frame_en_mask) != 0 &&
            (value & dd_mode_mask) == 0 &&
            (value & system_ready_mask) != 0 &&
            (int_status & runtime_error_mask) == 0 && fsync_seen) {
            printf("[PhaseCalib] Cheetah frame start verified: "
                   "0x1053=0x%08x, 0x10D2=0x%08x\n",
                   value, int_status);
            return 0;
        }

        if (ret == 0 && ((value & dd_mode_mask) != 0 ||
                         (int_status & runtime_error_mask) != 0)) {
            printf("[PhaseCalib] Cheetah runtime state rejected after "
                   "data-flow start: 0x1053=0x%08x, 0x10D2=0x%08x\n",
                   value, int_status);
            return -1;
        }

        if (ret == 0 && (value & frame_en_mask) != 0 &&
            (value & dd_mode_mask) == 0 &&
            (value & system_ready_mask) != 0 && !fsync_seen) {
            printf("[PhaseCalib] No fresh FSync after data-flow start: "
                   "0x1053=0x%08x, 0x10D2=0x%08x\n",
                   value, int_status);
            return -1;
        }

        printf("cheetah_start attempt %d failed: ret=%d, 0x1053=0x%08x, "
               "0x10D2=0x%08x\n", attempt, ret, value, int_status);
        mdelay(10);
    }

    return ret != 0 ? ret : -1;
}

void cheetah_stop(void)
{
   cheetah_reg_write_single(0, 0x1053,0x00000000, CHEETAH_CHECK);
   mdelay(100);
   cheetah_reg_write_single(0, 0x2003,0x800FF000, CHEETAH_CHECK);
   mdelay(100);

}

void cheetah_stop_for_reconfig(void)
{
    //cheetah_reg_write_single(0, 0x1053, 0x00000000, CHEETAH_CHECK);
    cheetah_stop();
    mdelay(100);
    cheetah_reset();
}

void cheetah_reset(void)
{
    cheetah_reg_write_single(0, 0x2009, 0x00000000, CHEETAH_CHECK);
    mdelay(100);
    cheetah_reg_write_single(0, 0x2009, 0x00000001, CHEETAH_CHECK);
}

static int reset_ram_impl(int devidx, int stop_on_error)
{
    uint16_t addr = 0x0000;
    uint32_t datalen = 97;
    uint32_t i;
    int first_error = 0;
    uint32_t pdata[16];

    // printf("ResetRams  %d \r\n", devidx);
    memset((void*) pdata, 0, sizeof(uint32_t) * 16);

    for (i = 0; i < datalen; i++)
    {
        int ret = cheetah_reg_write_burst(devidx, addr + (i * 16), pdata,
                                          16, CHEETAH_CHECK);
        if (ret != 0) {
            if (stop_on_error)
                return ret;
            if (first_error == 0)
                first_error = ret;
        }
    }

    return first_error;
}

void reset_ram(int devidx)
{
    reset_ram_impl(devidx, 0);
}

static int set_regs_to_target_impl(struct reg_line *regscfg, int count,
                                   int stop_on_error)
{
    int i;
    int delay;
    int chipIdx;
    int ret = 0;
    int first_error = 0;

    /* External functions for phase-shifter calibration command support */
    extern int radar_capture_host_start(void);
    extern void configure_host_frame_count(int input_cnt);
    extern int host_send_config_info(int chirp_num, int sample_num);

    if (regscfg == NULL || count <= 0)
        return -1;

    for(i = 0; i < count; i ++)
    {
        ret = 0;
        if(regscfg[i].addr == CMD_DELAY)
        {
            delay = regscfg[i].value[0];
            mdelay(delay);
        }
        else if(regscfg[i].addr == CMD_RESET_RAMS)
        {
            chipIdx = regscfg[i].chipIdx;
            ret = reset_ram_impl(chipIdx, stop_on_error);
        }
        else if(regscfg[i].addr == CMD_RADAR_START)
        {
            ret = radar_capture_host_start();
        }
        else if(regscfg[i].addr == CMD_SET_MAX_FRAME_CNT)
        {
            configure_host_frame_count(regscfg[i].value[0]);
            ret = 0;
        }
        else if(regscfg[i].addr == CMD_SEND_CONFIGINFO)
        {
            /* value[0] = subframe_num, value[1] = chirp_num, value[2] = sample_num */
            ret = host_send_config_info(regscfg[i].value[1],
                                        regscfg[i].value[2]);
        }
        else
        {
            ret = cheetah_reg_write_burst(regscfg[i].chipIdx,
                                          regscfg[i].addr,
                                          regscfg[i].value,
                                          regscfg[i].valLen,
                                          CHEETAH_CHECK);
        }

        if (ret != 0) {
            if (stop_on_error) {
                printf("cheetah config failed: index=%d, chip=%d, addr=0x%04x, "
                       "len=%d, ret=%d\n", i, regscfg[i].chipIdx,
                       (uint16_t)regscfg[i].addr, regscfg[i].valLen, ret);
                return ret;
            }
            if (first_error == 0)
                first_error = ret;
        }
    }

    return first_error;
}

int set_regs_to_target_checked(struct reg_line *regscfg, int count)
{
    return set_regs_to_target_impl(regscfg, count, 1);
}

void set_regs_to_target(struct reg_line *regscfg, int count)
{
    set_regs_to_target_impl(regscfg, count, 0);
}
