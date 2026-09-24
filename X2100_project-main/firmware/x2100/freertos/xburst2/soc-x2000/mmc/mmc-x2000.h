/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */


#ifndef __MMC_X2000_H__
#define __MMC_X2000_H__

#include <stdint.h>

typedef uint32_t                        dma_addr_t;

enum cmd_response_type {
    MSC_CMD_RESP_TYPE_NONE          = 0,  /* length 0 */
    MSC_CMD_RESP_TYPE_LONG          = 1,  /* length 136 */
    MSC_CMD_RESP_TYPE_SHORT         = 2,  /* length 48 */
    MSC_CMD_RESP_TYPE_SHORT_BUSY    = 3,  /* length 48 */
};

#endif /* __MMC_X2000_H__ */
