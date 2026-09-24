/*
 * Copyright (c) 2023, Ingenic Semiconductor
 *
 */

#ifndef __MMC_SDIO_IO_H__
#define __MMC_SDIO_IO_H__

#include <stdint.h>
#include "mmc-sdio-cis.h"

void sdio_claim_host(struct mmc_host *host);

void sdio_release_host(struct mmc_host *host);

int sdio_enable_func(struct sdio_func *func);

int sdio_disable_func(struct sdio_func *func);

int sdio_set_block_size(struct sdio_func *func, unsigned blksz);

uint8_t sdio_readb(struct sdio_func *func, unsigned int addr, int *err_ret);

void sdio_writeb(struct sdio_func *func, uint8_t b, unsigned int addr, int *err_ret);

uint8_t sdio_writeb_readb(struct sdio_func *func, uint8_t write_byte, unsigned int addr, int *err_ret);

int sdio_memcpy_fromio(struct sdio_func *func, void *dst, unsigned int addr, int count);

int sdio_memcpy_toio(struct sdio_func *func, unsigned int addr, void *src, int count);

int sdio_readsb(struct sdio_func *func, void *dst, unsigned int addr, int count);

int sdio_writesb(struct sdio_func *func, unsigned int addr, void *src, int count);

uint16_t sdio_readw(struct sdio_func *func, unsigned int addr, int *err_ret);

void sdio_writew(struct sdio_func *func, uint16_t b, unsigned int addr, int *err_ret);

uint32_t sdio_readl(struct sdio_func *func, unsigned int addr, int *err_ret);

void sdio_writel(struct sdio_func *func, uint32_t b, unsigned int addr, int *err_ret);

unsigned char sdio_f0_readb(struct sdio_func *func, unsigned int addr, int *err_ret);

void sdio_f0_writeb(struct sdio_func *func, unsigned char b, unsigned int addr, int *err_ret);

#endif /* __MMC_SDIO_IO_H__ */