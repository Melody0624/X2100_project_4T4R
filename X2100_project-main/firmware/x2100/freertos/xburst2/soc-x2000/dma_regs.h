#ifndef _DMA_REGS_H_
#define _DMA_REGS_H_

#define DSA(n) (0x00 + (n) * 0x20) /* channel n Source Address */
#define DTA(n) (0x04 + (n) * 0x20) /* channel n Target Address */
#define DTC(n) (0x08 + (n) * 0x20) /* channel n Transfer Count */
#define DRT(n) (0x0C + (n) * 0x20) /* channel n Request Source */
#define DCS(n) (0x10 + (n) * 0x20) /* channel n Control/Status */
#define DCM(n) (0x14 + (n) * 0x20) /* channel n Command */
#define DDA(n) (0x18 + (n) * 0x20) /* channel n Descriptor Address */
#define DSD(n) (0x1C + (n) * 0x20) /* channel n Stride Difference */

#define DMAC   0x1000 /* DMA Control */
#define DIRQP  0x1004 /* DMA Interrupt Pending */
#define DDB    0x1008 /* DMA Doorbell */
#define DDS    0x100C /* DMA Doorbell Set */
#define DMACP  0x101C /* DMA Channel Programmable */

#define DCS_NDES 31, 31
#define DCS_DES8 30, 30
#define DCS_CDOA 8, 15
#define DCS_AR 4, 4
#define DCS_TT 3, 3
#define DCS_HLT 2, 2
#define DCS_CTE 0, 0

#define DCM_SAI 23, 23
#define DCM_DAI 22, 22
#define DCM_RDIL 16, 19
#define DCM_SP 14, 15
#define DCM_DP 12, 13
#define DCM_TSZ 8, 10
#define DCM_STDE 2, 2
#define DCM_TIE 1, 1
#define DCM_LINK 0, 0

#define DDA_DBA 12, 31
#define DDA_DOA 4, 11

#define DSD_TSD 16, 31
#define DSD_SSD 0, 15

#define DMAC_FMSC 31, 31
#define DMAC_FSSI 30, 30
#define DMAC_FTSSI 29, 29
#define DMAC_FUART 28, 28
#define DMAC_FAIC 27, 27
#define DMAC_HLT 3, 3
#define DMAC_AR 2, 2
#define DMAC_DMAE 0, 0

#define DTC_DOA 24, 31
#define DTC_DTC 0, 23

#endif /* _DMA_REGS_H_ */
