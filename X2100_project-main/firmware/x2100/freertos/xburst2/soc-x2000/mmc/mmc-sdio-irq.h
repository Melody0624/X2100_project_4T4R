/*
 * Copyright (c) 2023, Ingenic Semiconductor
 *
 */

#ifndef __MMC_SDIO_IRQ_H__
#define __MMC_SDIO_IRQ_H__

#include <stdint.h>
#include "mmc-sdio-cis.h"

void sdio_run_irqs(struct mmc_host *host);

int sdio_claim_irq(struct sdio_func *func, sdio_irq_handler_t *handler);

int sdio_release_irq(struct sdio_func *func);

#endif /* __MMC_SDIO_IRQ_H__ */
