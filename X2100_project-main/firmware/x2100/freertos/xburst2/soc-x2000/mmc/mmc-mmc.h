/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */

#ifndef __MMC_MMC_H__
#define __MMC_MMC_H__

#include "mmc-core.h"
#include "card.h"

#define MMC_DATA_READ                   (1 << 0)
#define MMC_DATA_WRITE                  (1 << 1)


/*
 * Standard MMC commands (4.1)          type   argument                 response
 */
/* Class 1 */
#define MMC_GO_IDLE_STATE               0   /* bc */
#define MMC_SEND_OP_COND                1   /* bcr  [31:0] OCR         R3 */
#define MMC_ALL_SEND_CID                2   /* bcr                     R2  */
#define MMC_SET_RELATIVE_ADDR           3   /* ac   [31:16] RCA        R1  */

#define MMC_SWITCH                      6   /* ac   [31:0] See below   R1b */
#define MMC_SELECT_CARD                 7   /* ac   [31:16] RCA        R1  */
#define MMC_SEND_EXT_CSD                8   /* adtc                    R1  */
#define MMC_SEND_CSD                    9   /* ac   [31:16] RCA        R2  */
#define MMC_STOP_TRANSMISSION           12   /* ac                     R1b */
#define MMC_SEND_STATUS                 13   /* ac   [31:16] RCA       R1  */

/* class 2 */
#define MMC_SET_BLOCKLEN                16  /* ac   [31:0] block len   R1  */
#define MMC_READ_SINGLE_BLOCK           17  /* adtc [31:0] data addr   R1  */
#define MMC_READ_MULTIPLE_BLOCK         18  /* adtc [31:0] data addr   R1  */
#define MMC_SEND_TUNING_BLOCK           19  /* adtc */
#define MMC_SEND_TUNING_BLOCK_HS200     21  /* adtc R1 */

/* class 3 */
#define MMC_WRITE_DAT_UNTIL_STOP        20   /* adtc [31:0] data addr   R1  */

/* class 4 */
#define MMC_SET_BLOCK_COUNT             23   /* adtc [31:0] data addr   R1  */
#define MMC_WRITE_BLOCK                 24   /* adtc [31:0] data addr   R1  */
#define MMC_WRITE_MULTIPLE_BLOCK        25   /* adtc                    R1  */
#define MMC_PROGRAM_CID                 26   /* adtc                    R1  */
#define MMC_PROGRAM_CSD                 27   /* adtc                    R1  */

/* class 5 */
#define MMC_ERASE_GROUP_START           35   /* ac   [31:0] data addr   R1  */
#define MMC_ERASE_GROUP_END             36   /* ac   [31:0] data addr   R1  */
#define MMC_ERASE                       38   /* ac                      R1b */

/* class 8 */
#define MMC_APP_CMD                     55  /* ac   [31:16] RCA         R1  */
#define MMC_GEN_CMD                     56  /* adtc [0] RD/WR           R1  */



/*
 * MMC status in R1, for native mode (SPI bits are different)
 * Type
 *   e : error bit
 *   s : status bit
 *   r : detected and set for the actual command response
 *   x : detected and set during command execution. the host must poll
 *           the card by sending status command in order to read these bits.
 * Clear condition
 *   a : according to the card state
 *   b : always related to the previous command. Reception of
 *           a valid command will clear it (with a delay of one command)
 *   c : clear by read
 */

#define R1_OUT_OF_RANGE                 (1 << 31)   /* er, c */
#define R1_ADDRESS_ERROR                (1 << 30)   /* erx, c */
#define R1_BLOCK_LEN_ERROR              (1 << 29)   /* er, c */
#define R1_ERASE_SEQ_ERROR              (1 << 28)   /* er, c */
#define R1_ERASE_PARAM                  (1 << 27)   /* ex, c */
#define R1_WP_VIOLATION                 (1 << 26)   /* erx, c */
#define R1_CARD_IS_LOCKED               (1 << 25)   /* sx, a */
#define R1_LOCK_UNLOCK_FAILED           (1 << 24)   /* erx, c */
#define R1_COM_CRC_ERROR                (1 << 23)   /* er, b */
#define R1_ILLEGAL_COMMAND              (1 << 22)   /* er, b */
#define R1_CARD_ECC_FAILED              (1 << 21)   /* ex, c */
#define R1_CC_ERROR                     (1 << 20)   /* erx, c */
#define R1_ERROR                        (1 << 19)   /* erx, c */
#define R1_UNDERRUN                     (1 << 18)   /* ex, c */
#define R1_OVERRUN                      (1 << 17)   /* ex, c */
#define R1_CID_CSD_OVERWRITE            (1 << 16)   /* erx, c, CID/CSD overwrite */
#define R1_WP_ERASE_SKIP                (1 << 15)   /* sx, c */
#define R1_CARD_ECC_DISABLED            (1 << 14)   /* sx, a */
#define R1_ERASE_RESET                  (1 << 13)   /* sr, c */
#define R1_STATUS(x)                    (x & 0xFFFFE000)
#define R1_CURRENT_STATE(x)             ((x & 0x00001E00) >> 9) /* sx, b (4 bits) */
#define R1_READY_FOR_DATA               (1 << 8)    /* sx, a */
#define R1_SWITCH_ERROR                 (1 << 7)    /* sx, c */
#define R1_EXCEPTION_EVENT              (1 << 6)    /* sr, a */
#define R1_APP_CMD                      (1 << 5)    /* sr, c */

#define R1_STATE_IDLE                   0
#define R1_STATE_READY                  1
#define R1_STATE_IDENT                  2
#define R1_STATE_STBY                   3
#define R1_STATE_TRAN                   4
#define R1_STATE_DATA                   5
#define R1_STATE_RCV                    6
#define R1_STATE_PRG                    7
#define R1_STATE_DIS                    8

/*
 * Card Type info
 */
#define MMC_TYPE_MMC                    (0)     /* MMC card */
#define MMC_TYPE_SD                     (1)     /* SD card */
#define MMC_TYPE_SDIO                   (2)     /* SDIO card */
#define MMC_TYPE_SD_COMBO               (3)     /* SD combo (IO+mem) card */

/*
 * Card status
 */
#define MMC_STATE_PRESENT               (1<<0)  /* present in sysfs */
#define MMC_STATE_READONLY              (1<<1)  /* card is read-only */
#define MMC_STATE_BLOCKADDR             (1<<2)  /* card uses block-addressing */
#define MMC_CARD_SDXC                   (1<<3)  /* card is SDXC */
#define MMC_CARD_REMOVED                (1<<4)  /* card has been removed */
#define MMC_STATE_DOING_BKOPS           (1<<5)  /* card is doing BKOPS */
#define MMC_STATE_SUSPENDED             (1<<6)  /* card is suspended */

/*
 * Card Command Classes (CCC)
 */
#define CCC_BASIC                       (1<<0)  /* (0) Basic protocol functions */
                                                /* (CMD0,1,2,3,4,7,9,10,12,13,15) */
                                                /* (and for SPI, CMD58,59) */
#define CCC_STREAM_READ                 (1<<1)  /* (1) Stream read commands */
                                                /* (CMD11) */

#define CCC_BLOCK_READ                  (1<<2)  /* (2) Block read commands */
                                                /* (CMD16,17,18) */
#define CCC_STREAM_WRITE                (1<<3)  /* (3) Stream write commands */
                                                /* (CMD20) */
#define CCC_BLOCK_WRITE	                (1<<4)  /* (4) Block write commands */
                                                /* (CMD16,24,25,26,27) */
#define CCC_ERASE                       (1<<5)  /* (5) Ability to erase blocks */
                                                /* (CMD32,33,34,35,36,37,38,39) */
#define CCC_WRITE_PROT                  (1<<6)  /* (6) Able to write protect blocks */
                                                /* (CMD28,29,30) */
#define CCC_LOCK_CARD                   (1<<7)  /* (7) Able to lock down card */
                                                /* (CMD16,CMD42) */
#define CCC_APP_SPEC                    (1<<8)  /* (8) Application specific */
                                                /* (CMD55,56,57,ACMD*) */
#define CCC_IO_MODE                     (1<<9)  /* (9) I/O mode */
                                                /* (CMD5,39,40,52,53) */
#define CCC_SWITCH                      (1<<10) /* (10) High speed switch */
                                                /* (CMD6,34,35,36,37,50) */
                                                /* (11) Reserved */
                                                /* (CMD?) */


/*
 * MMC_SWITCH access modes
 */
#define MMC_SWITCH_MODE_CMD_SET         0x00    /* Change the command set */
#define MMC_SWITCH_MODE_SET_BITS        0x01    /* Set bits which are 1 in value */
#define MMC_SWITCH_MODE_CLEAR_BITS      0x02    /* Clear bits which are 1 in value */
#define MMC_SWITCH_MODE_WRITE_BYTE      0x03    /* Set target to value */

#define CSD_SPEC_VER_0                  0       /* Implements system specification 1.0 - 1.2 */
#define CSD_SPEC_VER_1                  1       /* Implements system specification 1.4 */
#define CSD_SPEC_VER_2                  2       /* Implements system specification 2.0 - 2.2 */
#define CSD_SPEC_VER_3                  3       /* Implements system specification 3.1 - 3.2 - 3.31 */
#define CSD_SPEC_VER_4                  4       /* Implements system specification 4.0 - 4.1 */

/*
 * EXT_CSD fields
 */
#define EXT_CSD_FLUSH_CACHE             32      /* W */
#define EXT_CSD_CACHE_CTRL              33      /* R/W */
#define EXT_CSD_POWER_OFF_NOTIFICATION  34      /* R/W */
#define EXT_CSD_PACKED_FAILURE_INDEX    35      /* RO */
#define EXT_CSD_PACKED_CMD_STATUS       36      /* RO */
#define EXT_CSD_EXP_EVENTS_STATUS       54      /* RO, 2 bytes */
#define EXT_CSD_EXP_EVENTS_CTRL         56      /* R/W, 2 bytes */
#define EXT_CSD_DATA_SECTOR_SIZE        61      /* R */
#define EXT_CSD_GP_SIZE_MULT            143     /* R/W */
#define EXT_CSD_PARTITION_ATTRIBUTE     156     /* R/W */
#define EXT_CSD_PARTITION_SUPPORT       160     /* RO */
#define EXT_CSD_HPI_MGMT                161     /* R/W */
#define EXT_CSD_RST_N_FUNCTION          162     /* R/W */
#define EXT_CSD_BKOPS_EN                163     /* R/W */
#define EXT_CSD_BKOPS_START             164     /* W */
#define EXT_CSD_SANITIZE_START          165     /* W */
#define EXT_CSD_WR_REL_PARAM            166     /* RO */
#define EXT_CSD_RPMB_MULT               168     /* RO */
#define EXT_CSD_BOOT_WP                 173     /* R/W */
#define EXT_CSD_ERASE_GROUP_DEF         175     /* R/W */
#define EXT_CSD_PART_CONFIG             179     /* R/W */
#define EXT_CSD_ERASED_MEM_CONT         181     /* RO */
#define EXT_CSD_BUS_WIDTH               183     /* R/W */
#define EXT_CSD_HS_TIMING               185     /* R/W */
#define EXT_CSD_POWER_CLASS             187     /* R/W */
#define EXT_CSD_REV                     192     /* RO */
#define EXT_CSD_STRUCTURE               194     /* RO */
#define EXT_CSD_CARD_TYPE               196     /* RO */
#define EXT_CSD_OUT_OF_INTERRUPT_TIME   198     /* RO */
#define EXT_CSD_PART_SWITCH_TIME        199     /* RO */
#define EXT_CSD_PWR_CL_52_195           200     /* RO */
#define EXT_CSD_PWR_CL_26_195           201     /* RO */
#define EXT_CSD_PWR_CL_52_360           202     /* RO */
#define EXT_CSD_PWR_CL_26_360           203     /* RO */
#define EXT_CSD_SEC_CNT                 212     /* RO, 4 bytes */
#define EXT_CSD_S_A_TIMEOUT             217     /* RO */
#define EXT_CSD_REL_WR_SEC_C            222     /* RO */
#define EXT_CSD_HC_WP_GRP_SIZE          221     /* RO */
#define EXT_CSD_ERASE_TIMEOUT_MULT      223     /* RO */
#define EXT_CSD_HC_ERASE_GRP_SIZE       224     /* RO */
#define EXT_CSD_BOOT_MULT               226     /* RO */
#define EXT_CSD_SEC_TRIM_MULT           229     /* RO */
#define EXT_CSD_SEC_ERASE_MULT          230     /* RO */
#define EXT_CSD_SEC_FEATURE_SUPPORT     231     /* RO */
#define EXT_CSD_TRIM_MULT               232     /* RO */
#define EXT_CSD_PWR_CL_200_195          236     /* RO */
#define EXT_CSD_PWR_CL_200_360          237     /* RO */
#define EXT_CSD_PWR_CL_DDR_52_195       238     /* RO */
#define EXT_CSD_PWR_CL_DDR_52_360       239     /* RO */
#define EXT_CSD_BKOPS_STATUS            246     /* RO */
#define EXT_CSD_POWER_OFF_LONG_TIME     247     /* RO */
#define EXT_CSD_GENERIC_CMD6_TIME       248     /* RO */
#define EXT_CSD_CACHE_SIZE              249     /* RO, 4 bytes */
#define EXT_CSD_TAG_UNIT_SIZE           498     /* RO */
#define EXT_CSD_DATA_TAG_SUPPORT        499     /* RO */
#define EXT_CSD_MAX_PACKED_WRITES       500     /* RO */
#define EXT_CSD_MAX_PACKED_READS        501     /* RO */
#define EXT_CSD_BKOPS_SUPPORT           502     /* RO */
#define EXT_CSD_HPI_FEATURES            503     /* RO */


/*
 * EXT_CSD field definitions
 */
#define EXT_CSD_WR_REL_PARAM_EN         (1<<2)

#define EXT_CSD_BOOT_WP_B_PWR_WP_DIS    (0x40)
#define EXT_CSD_BOOT_WP_B_PERM_WP_DIS   (0x10)
#define EXT_CSD_BOOT_WP_B_PERM_WP_EN    (0x04)
#define EXT_CSD_BOOT_WP_B_PWR_WP_EN     (0x01)

#define EXT_CSD_PART_CONFIG_ACC_MASK    (0x7)
#define EXT_CSD_PART_CONFIG_ACC_BOOT0   (0x1)
#define EXT_CSD_PART_CONFIG_ACC_RPMB    (0x3)
#define EXT_CSD_PART_CONFIG_ACC_GP0     (0x4)

#define EXT_CSD_PART_SUPPORT_PART_EN    (0x1)

#define EXT_CSD_CMD_SET_NORMAL          (1<<0)
#define EXT_CSD_CMD_SET_SECURE          (1<<1)
#define EXT_CSD_CMD_SET_CPSECURE        (1<<2)


#define EXT_CSD_CARD_TYPE_HS_26         (1<<0)  /* Card can run at 26MHz */
#define EXT_CSD_CARD_TYPE_HS_52         (1<<1)  /* Card can run at 52MHz */
#define EXT_CSD_CARD_TYPE_HS            (EXT_CSD_CARD_TYPE_HS_26 | EXT_CSD_CARD_TYPE_HS_52)
#define EXT_CSD_CARD_TYPE_DDR_1_8V      (1<<2)  /* Card can run at 52MHz */
                                                /* DDR mode @1.8V or 3V I/O */
#define EXT_CSD_CARD_TYPE_DDR_1_2V      (1<<3)  /* Card can run at 52MHz */
                                                /* DDR mode @1.2V I/O */
#define EXT_CSD_CARD_TYPE_DDR_52        (EXT_CSD_CARD_TYPE_DDR_1_8V | EXT_CSD_CARD_TYPE_DDR_1_2V)
#define EXT_CSD_CARD_TYPE_HS200_1_8V    (1<<4)  /* Card can run at 200MHz */
#define EXT_CSD_CARD_TYPE_HS200_1_2V    (1<<5)  /* Card can run at 200MHz */
                                                /* SDR mode @1.2V I/O */
#define EXT_CSD_CARD_TYPE_HS200         (EXT_CSD_CARD_TYPE_HS200_1_8V | EXT_CSD_CARD_TYPE_HS200_1_2V)
#define EXT_CSD_CARD_TYPE_HS400_1_8V    (1<<6)  /* Card can run at 200MHz DDR, 1.8V */
#define EXT_CSD_CARD_TYPE_HS400_1_2V    (1<<7)  /* Card can run at 200MHz DDR, 1.2V */
#define EXT_CSD_CARD_TYPE_HS400         (EXT_CSD_CARD_TYPE_HS400_1_8V | EXT_CSD_CARD_TYPE_HS400_1_2V)

#define EXT_CSD_BUS_WIDTH_1             0       /* Card is in 1 bit mode */
#define EXT_CSD_BUS_WIDTH_4             1       /* Card is in 4 bit mode */
#define EXT_CSD_BUS_WIDTH_8             2       /* Card is in 8 bit mode */
#define EXT_CSD_DDR_BUS_WIDTH_4         5       /* Card is in 4 bit DDR mode */
#define EXT_CSD_DDR_BUS_WIDTH_8         6       /* Card is in 8 bit DDR mode */

#define EXT_CSD_TIMING_BC               0       /* Backwards compatility */
#define EXT_CSD_TIMING_HS               1       /* High speed */
#define EXT_CSD_TIMING_HS200            2       /* HS200 */
#define EXT_CSD_TIMING_HS400            3       /* HS400 */
#define EXT_CSD_DRV_STR_SHIFT           4       /* Driver Strength shift */

#define EXT_CSD_SEC_ER_EN               BIT(0)
#define EXT_CSD_SEC_BD_BLK_EN           BIT(2)
#define EXT_CSD_SEC_GB_CL_EN            BIT(4)
#define EXT_CSD_SEC_SANITIZE            BIT(6)  /* v4.5 only */

#define EXT_CSD_RST_N_EN_MASK           0x3
#define EXT_CSD_RST_N_ENABLED           1       /* RST_n is enabled on card */

#define EXT_CSD_NO_POWER_NOTIFICATION   0
#define EXT_CSD_POWER_ON                1
#define EXT_CSD_POWER_OFF_SHORT         2
#define EXT_CSD_POWER_OFF_LONG          3

#define EXT_CSD_PWR_CL_8BIT_MASK        0xF0    /* 8 bit PWR CLS */
#define EXT_CSD_PWR_CL_4BIT_MASK        0x0F    /* 8 bit PWR CLS */
#define EXT_CSD_PWR_CL_8BIT_SHIFT       4
#define EXT_CSD_PWR_CL_4BIT_SHIFT       0

#define EXT_CSD_PACKED_EVENT_EN         BIT(3)

/*
 * EXCEPTION_EVENT_STATUS field
 */
#define EXT_CSD_URGENT_BKOPS            BIT(0)
#define EXT_CSD_DYNCAP_NEEDED           BIT(1)
#define EXT_CSD_SYSPOOL_EXHAUSTED       BIT(2)
#define EXT_CSD_PACKED_FAILURE          BIT(3)

#define EXT_CSD_PACKED_GENERIC_ERROR    BIT(0)
#define EXT_CSD_PACKED_INDEXED_ERROR    BIT(1)

/*
 * BKOPS status level
 */
#define EXT_CSD_BKOPS_LEVEL_2           0x2


/* Host capabilities */
#define MMC_CAP_4_BIT_DATA              (1 << 0)    /* Can the host do 4 bit transfers */
#define MMC_CAP_MMC_HIGHSPEED           (1 << 1)    /* Can do MMC high-speed timing */
#define MMC_CAP_SD_HIGHSPEED            (1 << 2)    /* Can do SD high-speed timing */
#define MMC_CAP_SDIO_IRQ                (1 << 3)    /* Can signal pending SDIO IRQs */
#define MMC_CAP_SPI                     (1 << 4)    /* Talks only SPI protocols */
#define MMC_CAP_NEEDS_POLL              (1 << 5)    /* Needs polling for card-detection */
#define MMC_CAP_8_BIT_DATA              (1 << 6)    /* Can the host do 8 bit transfers */

#define MMC_CAP_NONREMOVABLE            (1 << 8)    /* Nonremovable e.g. eMMC */
#define MMC_CAP_WAIT_WHILE_BUSY         (1 << 9)    /* Waits while card is busy */
#define MMC_CAP_ERASE                   (1 << 10)   /* Allow erase/trim commands */
#define MMC_CAP_1_8V_DDR                (1 << 11)   /* can support */
                                                    /* DDR mode at 1.8V */
#define MMC_CAP_1_2V_DDR                (1 << 12)   /* can support */
                                                    /* DDR mode at 1.2V */
#define MMC_CAP_POWER_OFF_CARD          (1 << 13)   /* Can power off after boot */
#define MMC_CAP_BUS_WIDTH_TEST          (1 << 14)   /* CMD14/CMD19 bus width ok */
#define MMC_CAP_UHS_SDR12               (1 << 15)   /* Host supports UHS SDR12 mode */
#define MMC_CAP_UHS_SDR25               (1 << 16)   /* Host supports UHS SDR25 mode */
#define MMC_CAP_UHS_SDR50               (1 << 17)   /* Host supports UHS SDR50 mode */
#define MMC_CAP_UHS_SDR104              (1 << 18)   /* Host supports UHS SDR104 mode */
#define MMC_CAP_UHS_DDR50               (1 << 19)   /* Host supports UHS DDR50 mode */
#define MMC_CAP_DRIVER_TYPE_A           (1 << 23)   /* Host supports Driver Type A */
#define MMC_CAP_DRIVER_TYPE_C           (1 << 24)   /* Host supports Driver Type C */
#define MMC_CAP_DRIVER_TYPE_D           (1 << 25)   /* Host supports Driver Type D */
#define MMC_CAP_CMD23                   (1 << 30)   /* CMD23 supported. */
#define MMC_CAP_HW_RESET                (1 << 31)   /* Hardware reset */

/* Host capabilities 2 */
#define MMC_CAP2_BOOTPART_NOACC         (1 << 0)    /* Boot partition no access */
#define MMC_CAP2_FULL_PWR_CYCLE         (1 << 2)    /* Can do full power cycle */
#define MMC_CAP2_HS200_1_8V_SDR         (1 << 5)    /* can support */
#define MMC_CAP2_HS200_1_2V_SDR         (1 << 6)    /* can support */
#define MMC_CAP2_HS200                  (MMC_CAP2_HS200_1_8V_SDR | MMC_CAP2_HS200_1_2V_SDR)
#define MMC_CAP2_HC_ERASE_SZ            (1 << 9)    /* High-capacity erase size */
#define MMC_CAP2_CD_ACTIVE_HIGH         (1 << 10)   /* Card-detect signal active high */
#define MMC_CAP2_RO_ACTIVE_HIGH         (1 << 11)   /* Write-protect signal active high */
#define MMC_CAP2_PACKED_RD              (1 << 12)   /* Allow packed read */
#define MMC_CAP2_PACKED_WR              (1 << 13)   /* Allow packed write */
#define MMC_CAP2_PACKED_CMD             (MMC_CAP2_PACKED_RD | MMC_CAP2_PACKED_WR)
#define MMC_CAP2_NO_PRESCAN_POWERUP     (1 << 14)   /* Don't power up before scan */
#define MMC_CAP2_HS400_1_8V             (1 << 15)   /* Can support HS400 1.8V */
#define MMC_CAP2_HS400_1_2V             (1 << 16)   /* Can support HS400 1.2V */
#define MMC_CAP2_HS400                  (MMC_CAP2_HS400_1_8V | MMC_CAP2_HS400_1_2V)
#define MMC_CAP2_HSX00_1_2V             (MMC_CAP2_HS200_1_2V_SDR | MMC_CAP2_HS400_1_2V)
#define MMC_CAP2_SDIO_IRQ_NOTHREAD      (1 << 17)
#define MMC_CAP2_NO_WRITE_PROTECT       (1 << 18)   /* No physical write protect pin, assume that card is always read-write */

/* card quirks */
#define MMC_QUIRK_LENIENT_FN0           (1<<0)  /* allow SDIO FN0 writes outside of the VS CCCR range */
#define MMC_QUIRK_BLKSZ_FOR_BYTE_MODE   (1<<1)  /* use func->cur_blksize */
                                                /* for byte mode */
#define MMC_QUIRK_NONSTD_SDIO           (1<<2)  /* non-standard SDIO card attached */
                                                /* (missing CIA registers) */
#define MMC_QUIRK_NONSTD_FUNC_IF        (1<<4)  /* SDIO card has nonstd function interfaces */
#define MMC_QUIRK_DISABLE_CD            (1<<5)  /* disconnect CD/DAT[3] resistor */
#define MMC_QUIRK_INAND_CMD38           (1<<6)  /* iNAND devices have broken CMD38 */
#define MMC_QUIRK_BLK_NO_CMD23          (1<<7)  /* Avoid CMD23 for regular multiblock */
#define MMC_QUIRK_BROKEN_BYTE_MODE_512  (1<<8)  /* Avoid sending 512 bytes in */
                                                /* byte mode */
#define MMC_QUIRK_LONG_READ_TIME        (1<<9)  /* Data read time > CSD says */
#define MMC_QUIRK_SEC_ERASE_TRIM_BROKEN (1<<10) /* Skip secure for erase/trim */
#define MMC_QUIRK_BROKEN_IRQ_POLLING    (1<<11) /* Polling SDIO_CCCR_INTx could create a fake interrupt */
#define MMC_QUIRK_TRIM_BROKEN           (1<<12) /* Skip trim */



struct mmc_card;

struct mmc_card_ops {
    int (*read)(struct mmc_card *card, uint32_t start, uint32_t count_blks, void *buffer);
    int (*write)(struct mmc_card *card, uint32_t start_blk, uint32_t count_blk, void *buffer);
    int (*erase)(struct mmc_card *card, int start_blk, int count_blk);
};

struct mmc_card {
    struct mmc_host *host;
    uint32_t type;          /* card type:SD or SDIO or MMC */
    uint32_t rca;           /* relative card address of device */
    uint32_t state;         /* (our) card state */
    uint32_t quirks;        /* card quirks */
    uint32_t raw_cid[4];    /* raw card CID */
    uint32_t raw_csd[4];    /* raw card CSD */
    uint32_t raw_scr[2];    /* raw card SCR */
    uint8_t *ext_csd;       /* EXT CSD 临时地址 */

    struct sd_scr scr;      /* extra SD information */
    struct sd_ssr ssr;      /* yet more SD information */
    struct sd_switch_caps sw_caps; /* switch (CMD6) caps */

    uint32_t tacc_clks;     /* data access time by clk cycles */
    uint32_t tacc_ns;       /* data access time by ns */
    uint32_t max_data_rate; /* max data transfer rate */
    uint64_t card_capacity; /* card capacity, unit:KB */
    uint32_t card_blk_size; /* card block size, uint Byte */
    uint32_t erase_size;    /* erase size in sectors. uint Byte */

    uint32_t flags; /* not use */
    uint32_t card_caps;
    uint32_t sd_bus_speed;   /* Bus Speed Mode set for the card */
    uint32_t mmc_avail_type; /* supported device type by both host and card */

    struct mmc_csd csd;      /* card specific */
    struct mmc_cid cid;      /* card specific */

    uint8_t part_config;
    uint64_t capacity_boot;
    uint64_t capacity_rpmb;
    uint64_t capacity_gp[4];

    uint32_t sdio_funcs;    /* number of SDIO functions */
    struct sdio_cccr cccr;  /* common card info */
    struct sdio_cis cis;    /* common tuple info */
    struct sdio_func *sdio_func[SDIO_MAX_FUNCS]; /* SDIO functions (devices) */
    struct sdio_func *sdio_single_irq; /* SDIO function when only one IRQ active */
    uint32_t num_info;      /* number of info strings */
    const char **info;      /* info strings */
    struct sdio_func_tuple *tuples; /* unknown common tuples */

    struct mmc_card_ops *ops;
};


#define mmc_card_mmc(c)                 ((c)->type == MMC_TYPE_MMC)
#define mmc_card_sd(c)                  ((c)->type == MMC_TYPE_SD)
#define mmc_card_sdio(c)                ((c)->type == MMC_TYPE_SDIO)

#define mmc_card_present(c)             ((c)->state & MMC_STATE_PRESENT)
#define mmc_card_readonly(c)            ((c)->state & MMC_STATE_READONLY)
#define mmc_card_blockaddr(c)           ((c)->state & MMC_STATE_BLOCKADDR)
#define mmc_card_ext_capacity(c)        ((c)->state & MMC_CARD_SDXC)
#define mmc_card_removed(c)             ((c) && ((c)->state & MMC_CARD_REMOVED))
#define mmc_card_doing_bkops(c)         ((c)->state & MMC_STATE_DOING_BKOPS)
#define mmc_card_suspended(c)           ((c)->state & MMC_STATE_SUSPENDED)

#define mmc_card_set_present(c)         ((c)->state |= MMC_STATE_PRESENT)
#define mmc_card_set_readonly(c)        ((c)->state |= MMC_STATE_READONLY)
#define mmc_card_set_blockaddr(c)       ((c)->state |= MMC_STATE_BLOCKADDR)
#define mmc_card_set_ext_capacity(c)    ((c)->state |= MMC_CARD_SDXC)
#define mmc_card_set_removed(c)         ((c)->state |= MMC_CARD_REMOVED)
#define mmc_card_set_doing_bkops(c)     ((c)->state |= MMC_STATE_DOING_BKOPS)
#define mmc_card_clr_doing_bkops(c)     ((c)->state &= ~MMC_STATE_DOING_BKOPS)
#define mmc_card_set_suspended(c)       ((c)->state |= MMC_STATE_SUSPENDED)
#define mmc_card_clr_suspended(c)       ((c)->state &= ~MMC_STATE_SUSPENDED)

/* 强制指定不支持Card Detect Disable功能 */
static inline int mmc_card_disable_cd(const struct mmc_card *c)
{
    /* 应该根据配置判断 MMC_QUIRK_DISABLE_CD标志 */
    return 0;
}

static inline int mmc_card_lenient_fn0(const struct mmc_card *c)
{
    return c->quirks & MMC_QUIRK_LENIENT_FN0;
}

static inline int mmc_blksz_for_byte_mode(const struct mmc_card *c)
{
    return c->quirks & MMC_QUIRK_BLKSZ_FOR_BYTE_MODE;
}

static inline int mmc_card_broken_byte_mode_512(const struct mmc_card *c)
{
    return c->quirks & MMC_QUIRK_BROKEN_BYTE_MODE_512;
}

static inline int mmc_card_broken_irq_polling(const struct mmc_card *c)
{
    return c->quirks & MMC_QUIRK_BROKEN_IRQ_POLLING;
}

#endif /* __MMC_MMC_H__ */
