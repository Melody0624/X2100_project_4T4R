/*
 * Copyright (C) 2023 Ingenic Semiconductor Co., Ltd.
 *
 * DDRC priority
 */

#include <common.h>
#include <bit_field.h>
#include <os.h>
#include <soc/base.h>

/* DDRC Controller Channel configure */
#define DDRC_PRIORITY_CHANNEL0          0x20   /* gmac & msc */
#define DDRC_PRIORITY_CHANNEL1          0x24   /* vpu & isp */
#define DDRC_PRIORITY_CHANNEL2          0x28
#define DDRC_PRIORITY_CHANNEL3          0x2C   /* dpu & cim */
#define DDRC_PRIORITY_CHANNEL4          0x30
#define DDRC_PRIORITY_CHANNEL5          0x34   /* ahb2 & audio & apb */
#define DDRC_PRIORITY_CHANNEL6          0x38   /* cpu */
#define DDRC_PRIORITY_CHANNEL7          0x3C

#define PORT_bandwidth_limit_write_en   7,7
#define PORT_bandwidth_limit_read_en    6,6
#define PORT_priority                   0,0



#define DDRC_ADDR(reg)               ((volatile unsigned long *)CKSEG1ADDR(DDRC_H0_IOBASE + reg))

static inline void ddrc_write_reg(unsigned int reg, unsigned int val)
{
    *DDRC_ADDR(reg) = val;
}

static inline unsigned int ddrc_read_reg(unsigned int reg)
{
    return *DDRC_ADDR(reg);
}


static inline void ddrc_set_bit(unsigned int reg, unsigned int start, unsigned int end, unsigned int val)
{
    set_bit_field_v(DDRC_ADDR(reg), start, end, val);
}

static inline unsigned int ddrc_get_bit(unsigned int reg, unsigned int start, unsigned int end)
{
    return get_bit_field_v(DDRC_ADDR(reg), start, end);
}


static int ddrc_adjust_controller_channel_priority(void)
{
    /* channel1 :VPU & ISP priotity to highest */
    ddrc_set_bit(DDRC_PRIORITY_CHANNEL1, PORT_priority, 1);

    /* channel6 :CPU port bandwidth limit Write/Read */
    ddrc_set_bit(DDRC_PRIORITY_CHANNEL6, PORT_bandwidth_limit_write_en, 1);
    ddrc_set_bit(DDRC_PRIORITY_CHANNEL6, PORT_bandwidth_limit_read_en, 1);
    ddrc_set_bit(DDRC_PRIORITY_CHANNEL6, PORT_priority, 0);

    //printf("Channel1(0x%x) = 0x%08x\n", DDRC_PRIORITY_CHANNEL1, ddrc_read_reg(DDRC_PRIORITY_CHANNEL1));
    //printf("Channel6(0x%x) = 0x%08x\n", DDRC_PRIORITY_CHANNEL6, ddrc_read_reg(DDRC_PRIORITY_CHANNEL6));

    return 0;
}
