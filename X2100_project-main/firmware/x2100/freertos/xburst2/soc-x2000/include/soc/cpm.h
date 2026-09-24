/*
 * JZSOC CPM register definition.
 *
 * CPM (Clock reset and Power control Management)
 *
 * Copyright (C) 2010 Ingenic Semiconductor Co., Ltd.
 */

#ifndef __CPM_H__
#define __CPM_H__

#include <io.h>
#include <soc/base.h>

#define CPM_CPCCR      0x00
#define CPM_CPCSR      0xD4
#define CPM_DDCDR      0x2C
#define CPM_MACPHYCDR  0x54
#define CPM_MACTXCDR   0x58
#define CPM_MACTXCDR1  0xDC
#define CPM_MACPTPCDC  0x4C
#define CPM_I2S0CDR    0x60
#define CPM_I2S0CDR1   0x70
#define CPM_I2S1CDR    0x7C
#define CPM_I2S1CDR1   0x80
#define CPM_I2S2CDR    0x84
#define CPM_I2S2CDR1   0x88
#define CPM_I2S3CDR    0x8C
#define CPM_I2S3CDR1   0xA0
#define CPM_AUDIOCR    0xAC
#define CPM_LPCDR      0x64
#define CPM_MSC0CDR    0x68
#define CPM_MSC1CDR    0xA4
#define CPM_MSC2CDR    0xA8
#define CPM_SFCCDR     0x74
#define CPM_SSICDR     0x5C
#define CPM_CIMCDR     0x78
#define CPM_PWMCDR     0x6C
#define CPM_ISPCDR     0x30
#define CPM_RSACDR     0x50
#define CPM_MACPHYC0   0xE4
#define CPM_MACPHYC1   0xE8
#define CPM_CPM_INTR   0xB0
#define CPM_CPM_INTRE  0xB4
#define CPM_CPM_SFTINT 0xBC
#define CPM_DRCG       0xD0
#define CPM_CPSPR      0x34
#define CPM_CPSPPR     0x38
#define CPM_USBPCR     0x3C
#define CPM_USBRDT     0x40
#define CPM_USBVBFIL   0x44
#define CPM_USBPCR1    0x48
#define CPM_CPPCR      0x0C
#define CPM_CPAPCR     0x10
#define CPM_CPMPCR     0x14
#define CPM_CPEPCR     0x18


#define CPM_LCR     0x04
#define CPM_PSWC0ST 0x90
#define CPM_PSWC1ST 0x94
#define CPM_PSWC2ST 0x98
#define CPM_PSWC3ST 0x9c
#define CPM_SRBC    0xC4
#define CPM_SLBC    0xC8
#define CPM_SLPC    0xCC
#define CPM_CLKGR0  0x20
#define CPM_CLKGR1  0x28
#define CPM_MEMPD0  0xF8
#define CPM_MEMPD1  0xFC
#define CPM_OPCR    0x24
#define CPM_MESTSEL 0xEC

#define CPM_RSR     0x08


#define LCR_LPM_MASK     (0x3)
#define LCR_LPM_SLEEP    (0x1)

#define OPCR_ERCS        (0x1<<2)
#define OPCR_IDLE        (0x1<<31)

#define XPCR_PLLFD     20, 28
#define XPCR_PLLRD     14, 19
#define XPCR_PLLOD     11, 13
#define XPCR_PLLRG     5, 7
#define XPCR_PLL_ON    3
#define XPCR_LOCK      2
#define XPCR_PLLEN     0

#define cpm_inl(off)             inl(CPM_IOBASE + (off))
#define cpm_outl(val,off)        outl(val,CPM_IOBASE + (off))
#define cpm_clear_bit(val,off)   do{cpm_outl((cpm_inl(off) & ~(1 << (val))),(off));}while(0)
#define cpm_set_bit(val,off)     do{cpm_outl((cpm_inl(off) | (1 << (val))),(off));}while(0)
#define cpm_test_bit(val,off)    (cpm_inl(off) & (0x1 << (val)))
#endif

/* __CPM_H__ */
