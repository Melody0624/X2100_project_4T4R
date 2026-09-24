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

    if (spi == NULL) {
        printf("cheetah: SPI1 registration failed\n");
        return -1;
    }
    return 0;
}

int cheetah_reg_read_single(int devidx, uint16_t addr, uint32_t *pdata, int check)
{

    int ret;
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




int cheetah_start(void)
{
    uint32_t value = 0x10000000;
    return cheetah_reg_write_single(0, 0x1053, value, CHEETAH_CHECK);
}

int cheetah_stop(void)
{
   int ret = cheetah_reg_write_single(
       0, 0x1053, 0x00000000, CHEETAH_CHECK);
   mdelay(100);
   if (cheetah_reg_write_single(
           0, 0x2003, 0x800FF000, CHEETAH_CHECK) < 0)
       ret = -1;
   mdelay(100);
   return ret;

}

int cheetah_reset(void)
{
    int ret = cheetah_reg_write_single(
        0, 0x2009, 0x00000000, CHEETAH_CHECK);
    mdelay(100);
    if (cheetah_reg_write_single(
            0, 0x2009, 0x00000001, CHEETAH_CHECK) < 0)
        ret = -1;
    return ret;
}

int reset_ram(int devidx)
{
    uint16_t addr = 0x0000;
    uint32_t datalen = 97;
    uint32_t i;
    uint32_t pdata[16];

    // printf("ResetRams  %d \r\n", devidx);
    memset((void*) pdata, 0, sizeof(uint32_t) * 16);

    for (i = 0; i < datalen; i++)
    {
        if (cheetah_reg_write_burst(
                devidx, addr + (i * 16), pdata, 16,
                CHEETAH_CHECK) < 0)
            return -1;
    }

    return 0;
}

int set_regs_to_target(struct reg_line *regscfg, int count)
{
    int i;
    int delay;
    int chipIdx;

    for(i = 0; i < count; i ++)
    {
        if(regscfg[i].addr == CMD_DELAY)
        {
            delay = regscfg[i].value[0];
            mdelay(delay);
        }
        else if(regscfg[i].addr == CMD_RESET_RAMS)
        {
            chipIdx = regscfg[i].chipIdx;
            if (reset_ram(chipIdx) < 0)
                return -1;
        }
        else
        {
            if (cheetah_reg_write_burst(
                    regscfg[i].chipIdx, regscfg[i].addr,
                    regscfg[i].value, regscfg[i].valLen,
                    CHEETAH_CHECK) < 0) {
                printf("Cheetah register write failed: index=%d addr=0x%04x\n",
                       i, (unsigned int)(uint16_t)regscfg[i].addr);
                return -1;
            }
        }
    }
    return 0;
}













