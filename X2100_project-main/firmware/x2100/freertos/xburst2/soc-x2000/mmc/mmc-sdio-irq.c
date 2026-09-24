/*
 * Copyright (c) 2023, Ingenic Semiconductor
 *
 */
#include <stdio.h>
#include <common.h>

#include "mmc-host.h"
#include "mmc-sdio.h"
#include "mmc-sdio-io.h"

//#define MMC_DEBUG

#ifdef MMC_DEBUG
#define MMC_DBG(...)                    printf("[SDIO-IRQ] Debug:"), printf(__VA_ARGS__)
#define MMC_WARN(...)                   printf("[SDIO-IRQ] Warn:"), printf(__VA_ARGS__)
#define MMC_ERR(...)                    printf("[SDIO-IRQ] Error:"), printf(__VA_ARGS__)
#else
#define MMC_DBG(...)
#define MMC_WARN(...)
#define MMC_ERR(...)                    printf("[SDIO-IRQ] Error:"), printf(__VA_ARGS__)
#endif


static int process_sdio_pending_irqs(struct mmc_host *host)
{
    struct mmc_card *card = host->card;
    int i, ret, count;
    unsigned char pending;
    struct sdio_func *func;

    /*
     * Optimization, if there is only 1 function interrupt registered
     * and we know an IRQ was signaled then call irq handler directly.
     * Otherwise do the full probe.
     */
    func = card->sdio_single_irq;
    if (func && host->sdio_irq_pending) {
        func->irq_handler(func);
        return 1;
    }


    ret = sdio_io_rw_direct(card, 0, 0, SDIO_CCCR_INTx, 0, &pending);
    if (ret) {
        printf("error %d reading SDIO_CCCR_INTx\n", ret);
        return ret;
    }

    if (pending && mmc_card_broken_irq_polling(card) &&
        !(host->capacity & MMC_CAP_SDIO_IRQ)) {
        unsigned char dummy;

        /* A fake interrupt could be created when we poll SDIO_CCCR_INTx
         * register with a Marvell SD8797 card. A dummy CMD52 read to
         * function 0 register 0xff can avoid this.
         */
        sdio_io_rw_direct(card, 0, 0, 0xff, 0, &dummy);
    }

    count = 0;
    for (i = 1; i <= 7; i++) {
        if (pending & (1 << i)) {
            func = card->sdio_func[i - 1];
            if (!func) {
                printf("pending IRQ for non-existent function\n");
                ret = -EINVAL;
            } else if (func->irq_handler) {
                func->irq_handler(func);
                count++;
            } else {
                printf("pending IRQ with no handler\n");
                ret = -EINVAL;
            }
        }
    }

    if (count)
        return count;

    return ret;
}

void sdio_run_irqs(struct mmc_host *host)
{
    sdio_claim_host(host);

    host->sdio_irq_pending = 1;
    process_sdio_pending_irqs(host);

    sdio_release_host(host);
}

static void sdio_irq_thread(void *_host)
{
    struct mmc_host *host = _host;
    unsigned long period;
    int ret;

    period = 100;

    MMC_DBG("IRQ thread started (poll period = %lu jiffies)\n",
          period);

    do {
        /*
         * We claim the host here on drivers behalf for a couple
         * reasons:
         *
         * 1) it is already needed to retrieve the CCCR_INTx;
         * 2) we want the driver(s) to clear the IRQ condition ASAP;
         * 3) we need to control the abort condition locally.
         *
         * Just like traditional hard IRQ handlers, we expect SDIO
         * IRQ handlers to be quick and to the point, so that the
         * holding of the host lock does not cover too much work
         * that doesn't require that lock to be held.
         */
        sdio_claim_host(host);
        ret = process_sdio_pending_irqs(host);
        host->sdio_irq_pending = 0;
        sdio_release_host(host);

        if (ret < 0) {
            if (!thread_is_deleted(host->sdio_irq_thread))
                thread_waiter_wait_timeout(&host->waiter, 100);
        }

        if (host->capacity & MMC_CAP_SDIO_IRQ) {
            host->enable_sdio_irq(host, 1);
        }

        if (!thread_is_deleted(host->sdio_irq_thread)) {
            thread_waiter_wait_timeout(&host->waiter, period);
        }

    } while (!thread_is_deleted(host->sdio_irq_thread));

    if (host->capacity & MMC_CAP_SDIO_IRQ)
        host->enable_sdio_irq(host, 0);

    MMC_DBG("IRQ thread exiting\n");
}


static int sdio_card_irq_get(struct mmc_card *card)
{
    struct mmc_host *host = card->host;

    assert(!host->claimed);

    if (!host->sdio_irqs++) {
        if (!(host->capacity2 & MMC_CAP2_SDIO_IRQ_NOTHREAD)) {
            thread_waiter_init(&card->host->waiter);
            host->sdio_irq_thread = thread_create("irq", 4096, sdio_irq_thread,  (void *)host);
            if (IS_ERR(host->sdio_irq_thread)) {
                int err = PTR_ERR(host->sdio_irq_thread);
                host->sdio_irqs--;
                return err;
            }
        } else if (host->capacity & MMC_CAP_SDIO_IRQ) {
                host->enable_sdio_irq(host, 1);
        }
    }

    return 0;
}

static int sdio_card_irq_put(struct mmc_card *card)
{
    struct mmc_host *host = card->host;

    assert(!host->claimed);
    assert(host->sdio_irqs < 1);

    if (!--host->sdio_irqs) {
        if (!(host->capacity2 & MMC_CAP2_SDIO_IRQ_NOTHREAD)) {
                thread_delete(host->sdio_irq_thread);
        } else if (host->capacity & MMC_CAP_SDIO_IRQ) {
                host->enable_sdio_irq(host, 0);
        }
    }

    return 0;
}

/* If there is only 1 function registered set sdio_single_irq */
static void sdio_single_irq_set(struct mmc_card *card)
{
    struct sdio_func *func;
    int i;

    card->sdio_single_irq = NULL;
    if ((card->host->capacity & MMC_CAP_SDIO_IRQ) &&
        card->host->sdio_irqs == 1)
        for (i = 0; i < card->sdio_funcs; i++) {
               func = card->sdio_func[i];
               if (func && func->irq_handler) {
                   card->sdio_single_irq = func;
                   break;
               }
           }
}

/**
 * sdio_claim_irq - claim the IRQ for a SDIO function
 * @func: SDIO function
 * @handler: IRQ handler callback
 *
 * Claim and activate the IRQ for the given SDIO function. The provided
 * handler will be called when that IRQ is asserted.  The host is always
 * claimed already when the handler is called so the handler must not
 * call sdio_claim_host() nor sdio_release_host().
 */
int sdio_claim_irq(struct sdio_func *func, sdio_irq_handler_t *handler)
{
    int ret;
    unsigned char reg;

    assert(func);
    assert(func->card);

    MMC_DBG("SDIO: Enabling IRQ...\n");

    if (func->irq_handler) {
        MMC_DBG("SDIO: IRQ already in use.\n");
        return -EBUSY;
    }

    ret = sdio_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IENx, 0, &reg);
    if (ret)
        return ret;

    reg |= 1 << func->num;

    reg |= 1; /* Master interrupt enable */

    ret = sdio_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IENx, reg, NULL);
    if (ret)
        return ret;

    func->irq_handler = handler;
    ret = sdio_card_irq_get(func->card);
    if (ret)
        func->irq_handler = NULL;
    sdio_single_irq_set(func->card);

    return ret;
}

/**
 * sdio_release_irq - release the IRQ for a SDIO function
 * @func: SDIO function
 *
 * Disable and release the IRQ for the given SDIO function.
 */
int sdio_release_irq(struct sdio_func *func)
{
    int ret;
    unsigned char reg;

    assert(func);
    assert(func->card);

    MMC_DBG("SDIO: Disabling IRQ...\n");

    if (func->irq_handler) {
        func->irq_handler = NULL;
        sdio_card_irq_put(func->card);
        sdio_single_irq_set(func->card);
    }

    ret = sdio_io_rw_direct(func->card, 0, 0, SDIO_CCCR_IENx, 0, &reg);
    if (ret)
        return ret;

    reg &= ~(1 << func->num);

    /* Disable master interrupt with the last function interrupt */
    if (!(reg & 0xFE))
        reg = 0;

    ret = sdio_io_rw_direct(func->card, 1, 0, SDIO_CCCR_IENx, reg, NULL);
    if (ret)
        return ret;

    return 0;
}

#include <kernel_symbol.h>
EXPORT_SYMBOL(sdio_run_irqs);
EXPORT_SYMBOL(sdio_claim_irq);
EXPORT_SYMBOL(sdio_release_irq);
