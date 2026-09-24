#ifndef __HELIX_BUF_H__
#define __HELIX_BUF_H__

#include <stdint.h>
#include <list.h>

enum helix_raw_format {
	HELIX_TILE_MODE = 0,
	HELIX_420P_MODE = 4,
	HELIX_NV12_MODE = 8,
	HELIX_NV21_MODE = 12,
};


/* JPEG encode quantization table select level */
typedef enum {
  LOW_QUALITY,
  MEDIUMS_QUALITY,
  HIGH_QUALITY
} QUANT_QUALITY;


#ifndef __DMA_ADDR_T_DEFINED
#define __DMA_ADDR_T_DEFINED
typedef uint32_t dma_addr_t;
#endif

struct h264_ref_tile {
	unsigned int *yaddr;
	unsigned int *caddr;

	dma_addr_t yaddr_pa;
	dma_addr_t caddr_pa;
};


/* basic memory description. */
struct ingenic_vcodec_mem {
	size_t size;
	void *va;
	dma_addr_t pa;
};

struct video_frame_buffer {
	struct ingenic_vcodec_mem fb_addr[3];// MAX_PLANES.
	int num_planes;
	int index;
	struct list_head queue_entry;
	struct list_head done_entry;
};



//extern int ingenic_vpu_start(struct h264e_ctx *h264e_ctx);

#endif
