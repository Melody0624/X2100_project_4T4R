/*
 * Copyright (C) 2021 Ingenic Semiconductor Co., Ltd.
 *
 * MMC driver for CPM
 *
 */

#ifndef __X2000_MMC_CPM_H__
#define __X2000_MMC_CPM_H__

#include <common.h>
#include <bit_field.h>
#include <soc/base.h>

/*
 * MSC外部时钟使能寄存器
 * MSC0,MSC1，MSC2 共用MSC0寄存器的bit21 选择打开/关闭外部时钟
 */
#define CPM_MSC_EXCLK                   (0x68)

#define CPM_MSC_EXCLK_ENABLE            21, 21


#define MSC_CPM_ADDR(reg)               ((volatile unsigned long *)CKSEG1ADDR(CPM_IOBASE + reg))

static inline void cpm_write_reg(unsigned int reg, unsigned int val)
{
    *MSC_CPM_ADDR(reg) = val;
}

static inline unsigned int cpm_read_reg(unsigned int reg)
{
    return *MSC_CPM_ADDR(reg);
}

static inline void cpm_set_bit_v(unsigned int reg, unsigned int start, unsigned int end, unsigned int val)
{
    set_bit_field_v(MSC_CPM_ADDR(reg), start, end, val);
}

static inline unsigned int cpm_get_bit_v(unsigned int reg, unsigned int start, unsigned int end)
{
    return get_bit_field_v(MSC_CPM_ADDR(reg), start, end);
}

#endif /* __X2000_MMC_CPM_H__ */
