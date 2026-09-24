/*
 * Copyright (c) 2019, Ingenic Semiconductor
 *
 */

#ifndef __MMC_CARD_H__
#define __MMC_CARD_H__

#include <stdint.h>

struct mmc_cid {
    uint32_t    manfid;
    char        prod_name[8];
    uint32_t    serial;
    uint16_t    oemid;
    uint16_t    year;
    uint8_t     prod_version;
    uint8_t     month;
};

struct mmc_csd {
    uint8_t     structure;      /* CSD register version */
    uint8_t     mmca_vsn;
    uint32_t    taac;
    uint32_t    nsac;
    uint32_t    tran_speed;     /* max data transfer rate */
    uint32_t    cmd_class;      /* card command classes */
    uint32_t    rd_blk_len;     /* max read data block length. unit:Byte */
    uint32_t    rd_blk_part;
    uint32_t    wr_blk_misalign;
    uint32_t    rd_blk_misalign;
    uint32_t    dsr_imp;        /* DSR implemented */
    uint32_t    c_size_mult;    /* CSD 1.0 , device size multiplier */
    uint32_t    c_size;         /* device size */
    uint32_t    r2w_factor;
    uint32_t    wr_blk_len;     /* max wtire data block length. unit:Byte */
    uint32_t    wr_blk_partial;
    uint32_t    csd_crc;
    uint32_t    w_protect;
};

struct sd_scr {
    uint8_t     sda_vsn;
    uint8_t     sda_spec3;
    uint8_t     bus_widths;
    uint8_t     cmds;
};

struct sd_ssr {
    uint32_t    au;             /* In sectors */
    uint32_t    erase_timeout;  /* In milliseconds */
    uint32_t    erase_offset;   /* In milliseconds */
};

struct sd_switch_caps {
    uint32_t    hs_max_dtr;
    uint32_t    uhs_max_dtr;
    uint32_t    sd3_bus_mode;
    uint32_t    sd3_drv_type;
    uint32_t    sd3_curr_limit;
};

/*
 * SDIO:以下 SDIO 相关结构体定义 为kernel目录下定义
 * include/linux/mmc/card.h
 */
struct sdio_cccr {
    uint8_t     sdio_version;
    uint8_t     sd_version;
    uint8_t     multi_block:1,  /* Card Supports Multi-Block */
                low_speed:1,    /* Card is a Low-Speed  card */
                wide_bus:1,     /* Support SDIO bus width, 1:4bit, 0:1bit */
                high_power:1,   /* Support High-Power: =1 card power support > 720mW(3.6Vx200mA)
                                 *                     =0 card power support <=720mW(3.6Vx200mA) */
                high_speed:1,   /* Support High-Speed  */
                disable_cd:1;   /*  Connect[0]/Disconnect[1] the 10K-90K ohm pull-up
                                    resistor on CD/DAT[3] (pin 1) of the card */
};

struct sdio_cis {
    uint16_t    vendor;
    uint16_t    device;
    uint16_t    blksize;        /* function0 block size */
    uint32_t    max_dtr;        /* max tranxfer speed */
};


#define SDIO_MAX_FUNCS                  7

#endif /* __MMC_CARD_H__ */
