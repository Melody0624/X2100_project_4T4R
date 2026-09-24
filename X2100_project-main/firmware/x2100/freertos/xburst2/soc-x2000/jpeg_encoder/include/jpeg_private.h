#ifndef __JPEG_PRIVATE_H__
#define __JPEG_PRIVATE__

typedef struct {
	uint8_t *YBuf[3];       /* YUV420: Y,U,V. NV12: Y UV */
	uint8_t *BitStreamBuf;
	unsigned int des_va;	/* descriptor virtual address */
	unsigned int des_pa;	/* descriptor physical address */
	uint32_t InDaMd;
	uint32_t nmcu;
	uint32_t nrsm;
	uint32_t width;
	uint32_t height;
	int format;	/*input format: NV12/NV21*/
	uint32_t bslen;
	uint32_t ncol;
	uint32_t rsm;
	uint32_t ql_sel;
	uint8_t huffenc_sel;
} JPEGE_SwInfo;

#endif	/* __JPEG_PRIVATE__ */
