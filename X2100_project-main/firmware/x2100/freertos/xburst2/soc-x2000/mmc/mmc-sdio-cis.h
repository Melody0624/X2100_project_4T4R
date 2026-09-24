/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */

#ifndef __MMC_SDIO_CIS_H__
#define __MMC_SDIO_CIS_H__

#include <stdint.h>
#include "mmc-mmc.h"

/*
 * SDIO function CIS tuple (unknown to the core)
 */
struct sdio_func_tuple {
    struct sdio_func_tuple *next;
    uint8_t code;
    uint8_t size;
    uint8_t data[0];
};


struct sdio_func;
typedef void (sdio_irq_handler_t)(struct sdio_func *);

/*
 * SDIO function devices
 */

struct mmc_card;

struct sdio_func {
    struct mmc_card     *card;              /* the card this device belongs to */
    sdio_irq_handler_t  *irq_handler;       /* IRQ callback */
    uint8_t             num;                /* function number */

    uint8_t             class;          /*  Standard SDIO Function interface code  */
    uint16_t            vendor;             /* vendor id */
    uint16_t            device;             /* device id */

    uint32_t            max_blksize;        /* maximum block size */
    uint32_t            cur_blksize;        /* current block size */

    uint32_t            enable_timeout;     /* max enable timeout in msec */
    uint32_t            state;              /* function state */

    uint8_t             *tmpbuf;           /* DMA:able scratch buffer */

    uint32_t            num_info;           /* number of info strings */
    const char          **info;             /* info strings */
    struct sdio_func_tuple *tuples;
    void                *priv;
};


int sdio_read_common_cis(struct mmc_card *card);
void sdio_free_common_cis(struct mmc_card *card);

int sdio_read_func_cis(struct sdio_func *func);
void sdio_free_func_cis(struct sdio_func *func);

#endif /* __MMC_SDIO_CIS_H__ */
