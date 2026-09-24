#ifndef __DESC_COM_H__
#define __DESC_COM_H__

#include <little_things.h>
#include "mac_common.h"
#include "descs.h"

/* Specific functions used for Ring mode */

/* Enhanced descriptors */
static inline void ehn_desc_rx_set_on_ring(struct dma_desc *p, int end, int bfsize)
{
	if (bfsize == BUF_SIZE_16KiB)
		p->des01.erx.buffer2_size = BUF_SIZE_8KiB;
	if (end)
		p->des01.erx.end_ring = 1;
}

static inline void ehn_desc_tx_set_on_ring(struct dma_desc *p, int end)
{
	if (end)
		p->des01.etx.end_ring = 1;
}

static inline void enh_desc_end_tx_desc_on_ring(struct dma_desc *p, int ter)
{
	p->des01.etx.end_ring = ter;
}

static inline void enh_set_tx_desc_len_on_ring(struct dma_desc *p, int len)
{
	if (unlikely(len > BUF_SIZE_4KiB)) {
		p->des01.etx.buffer1_size = BUF_SIZE_4KiB;
		p->des01.etx.buffer2_size = len - BUF_SIZE_4KiB;
	} else
		p->des01.etx.buffer1_size = len;
}

/* Normal descriptors */
static inline void ndesc_rx_set_on_ring(struct dma_desc *p, int end, int bfsize)
{
	int size;

	if (bfsize >= BUF_SIZE_2KiB) {
		size = min(bfsize - BUF_SIZE_2KiB + 1, BUF_SIZE_2KiB - 1);
		p->des01.rx.buffer2_size = size;
	}
	if (end)
		p->des01.rx.end_ring = 1;
}

static inline void ndesc_tx_set_on_ring(struct dma_desc *p, int end)
{
	if (end)
		p->des01.tx.end_ring = 1;
}

static inline void ndesc_end_tx_desc_on_ring(struct dma_desc *p, int ter)
{
	p->des01.tx.end_ring = ter;
}

static inline void norm_set_tx_desc_len_on_ring(struct dma_desc *p, int len)
{
	if (unlikely(len > BUF_SIZE_2KiB)) {
		p->des01.etx.buffer1_size = BUF_SIZE_2KiB - 1;
		p->des01.etx.buffer2_size = len - p->des01.etx.buffer1_size;
	} else
		p->des01.tx.buffer1_size = len;
}

/* Specific functions used for Chain mode */

/* Enhanced descriptors */
static inline void ehn_desc_rx_set_on_chain(struct dma_desc *p, int end)
{
	p->des01.erx.second_address_chained = 1;
}

static inline void ehn_desc_tx_set_on_chain(struct dma_desc *p, int end)
{
	p->des01.etx.second_address_chained = 1;
}

static inline void enh_desc_end_tx_desc_on_chain(struct dma_desc *p, int ter)
{
	p->des01.etx.second_address_chained = 1;
}

static inline void enh_set_tx_desc_len_on_chain(struct dma_desc *p, int len)
{
	p->des01.etx.buffer1_size = len;
}

/* Normal descriptors */
static inline void ndesc_rx_set_on_chain(struct dma_desc *p, int end)
{
	p->des01.rx.second_address_chained = 1;
}

static inline void ndesc_tx_set_on_chain(struct dma_desc *p, int ring_size)
{
	p->des01.tx.second_address_chained = 1;
}

static inline void ndesc_end_tx_desc_on_chain(struct dma_desc *p, int ter)
{
	p->des01.tx.second_address_chained = 1;
}

static inline void norm_set_tx_desc_len_on_chain(struct dma_desc *p, int len)
{
	p->des01.tx.buffer1_size = len;
}
#endif /* __DESC_COM_H__ */
