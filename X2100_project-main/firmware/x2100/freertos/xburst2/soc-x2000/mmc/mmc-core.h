/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */

#ifndef __MMC_CORE_H__
#define __MMC_CORE_H__

#include <stdint.h>
#include <errno.h>
#include <mmc-mmc.h>
#include <os.h>
#include <spl_rtos_argument.h>

/* data bus width */
#define MMC_BUS_WIDTH_MASK              0x3
#define MMC_BUS_WIDTH_1                 0
#define MMC_BUS_WIDTH_4                 2
#define MMC_BUS_WIDTH_8                 3

/*
 * MMC Command Responses
 */
#define MMC_RSP_PRESENT                 (1 << 0)
#define MMC_RSP_136                     (1 << 1)        /* 136 bit response */
#define MMC_RSP_CRC                     (1 << 2)        /* expect valid crc */
#define MMC_RSP_BUSY                    (1 << 3)        /* card may send busy */
#define MMC_RSP_OPCODE                  (1 << 4)        /* response contains opcode */

#define MMC_CMD_MASK                    (3 << 5)        /* non-SPI command type */
#define MMC_CMD_AC                      (0 << 5)
#define MMC_CMD_ADTC                    (1 << 5)
#define MMC_CMD_BC                      (2 << 5)
#define MMC_CMD_BCR                     (3 << 5)


/*
 * These are the native response types, and correspond to valid bit
 * patterns of the above flags.  One additional valid pattern
 * is all zeros, which means we don't expect a response.
 */
#define MMC_RSP_NONE                    (0)
#define MMC_RSP_R1                      (MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE)
#define MMC_RSP_R1B                     (MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE | MMC_RSP_BUSY)
#define MMC_RSP_R2                      (MMC_RSP_PRESENT | MMC_RSP_136 | MMC_RSP_CRC)
#define MMC_RSP_R3                      (MMC_RSP_PRESENT)
#define MMC_RSP_R4                      (MMC_RSP_PRESENT)
#define MMC_RSP_R5                      (MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE)
#define MMC_RSP_R6                      (MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE)
#define MMC_RSP_R7                      (MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE)

#define mmc_resp_type(cmd)              ((cmd)->resp_type & \
                                         (MMC_RSP_PRESENT|MMC_RSP_136|MMC_RSP_CRC|MMC_RSP_BUSY|MMC_RSP_OPCODE))

#define MMC_VDD_165_195                 0x00000080  /* VDD voltage 1.65 - 1.95 */
#define MMC_VDD_20_21                   0x00000100  /* VDD voltage 2.0 ~ 2.1 */
#define MMC_VDD_21_22                   0x00000200  /* VDD voltage 2.1 ~ 2.2 */
#define MMC_VDD_22_23                   0x00000400  /* VDD voltage 2.2 ~ 2.3 */
#define MMC_VDD_23_24                   0x00000800  /* VDD voltage 2.3 ~ 2.4 */
#define MMC_VDD_24_25                   0x00001000  /* VDD voltage 2.4 ~ 2.5 */
#define MMC_VDD_25_26                   0x00002000  /* VDD voltage 2.5 ~ 2.6 */
#define MMC_VDD_26_27                   0x00004000  /* VDD voltage 2.6 ~ 2.7 */
#define MMC_VDD_27_28                   0x00008000  /* VDD voltage 2.7 ~ 2.8 */
#define MMC_VDD_28_29                   0x00010000  /* VDD voltage 2.8 ~ 2.9 */
#define MMC_VDD_29_30                   0x00020000  /* VDD voltage 2.9 ~ 3.0 */
#define MMC_VDD_30_31                   0x00040000  /* VDD voltage 3.0 ~ 3.1 */
#define MMC_VDD_31_32                   0x00080000  /* VDD voltage 3.1 ~ 3.2 */
#define MMC_VDD_32_33                   0x00100000  /* VDD voltage 3.2 ~ 3.3 */
#define MMC_VDD_33_34                   0x00200000  /* VDD voltage 3.3 ~ 3.4 */
#define MMC_VDD_34_35                   0x00400000  /* VDD voltage 3.4 ~ 3.5 */
#define MMC_VDD_35_36                   0x00800000  /* VDD voltage 3.5 ~ 3.6 */

/*
 * OCR bits are mostly in host.h
 */
#define MMC_CARD_BUSY                   0x80000000  /* Card Power up status bit */

/*
 * MMC Erase
 */
#define MMC_ERASE_ARG                   0x00000000
#define MMC_SECURE_ERASE_ARG            0x80000000
#define MMC_TRIM_ARG                    0x00000001
#define MMC_DISCARD_ARG                 0x00000003
#define MMC_SECURE_TRIM1_ARG            0x80000001
#define MMC_SECURE_TRIM2_ARG            0x80008000

#define MMC_SECURE_ARGS                 0x80000000
#define MMC_TRIM_ARGS                   0x00008001

/*
 * power mode
 */
#define MMC_POWER_OFF                   0
#define MMC_POWER_UP                    1
#define MMC_POWER_ON                    2

/*
 * timing specification used
 */
#define MMC_TIMING_LEGACY               0
#define MMC_TIMING_MMC_HS               1
#define MMC_TIMING_SD_HS                2
#define MMC_TIMING_UHS_SDR12            3
#define MMC_TIMING_UHS_SDR25            4
#define MMC_TIMING_UHS_SDR50            5
#define MMC_TIMING_UHS_SDR104           6
#define MMC_TIMING_UHS_DDR50            7
#define MMC_TIMING_MMC_DDR52            8
#define MMC_TIMING_MMC_HS200            9
#define MMC_TIMING_MMC_HS400            10


/*
 * signalling voltage (1.8V or 3.3V)
 */
#define MMC_SIGNAL_VOLTAGE_330          0
#define MMC_SIGNAL_VOLTAGE_180          1
#define MMC_SIGNAL_VOLTAGE_120          2

/*
 * driver type (A, B, C, D)
 */
#define MMC_SET_DRIVER_TYPE_B           0
#define MMC_SET_DRIVER_TYPE_A           1
#define MMC_SET_DRIVER_TYPE_C           2
#define MMC_SET_DRIVER_TYPE_D           3

struct mmc_data {
    union {
        char *dest;
        const char *src;
    };
    uint32_t blksz;      /* data block size */
    uint32_t blocks;     /* number of blocks */
    uint32_t error;      /* data error */
    uint32_t flags;
    uint32_t bytes_xfered; /* 传输完成字节数 */
};

struct mmc_cmd {
    uint32_t opcode;
    uint32_t arg;
    uint32_t resp[4];
    uint32_t resp_type;
    uint32_t retries;
    uint32_t error;
};


struct mmc_host {
    int index;
    struct mmc_card *card;
    char name[16];
    uint32_t voltages;
    uint32_t capacity;
    uint32_t capacity2;

    uint32_t version;       /* host controller version */
    uint32_t bus_width;
    uint32_t power_mode;    /* power supply mode:MMC_POWER_OFF / MMC_POWER_ON / MMC_POWER_UP */
    uint16_t min_voltage;   /* OCR指示支持最低工作电压 */
    uint8_t  timing;        /* timing specification used */
    uint32_t max_current_330;/* for SD Host Controller spec V3.0 */
    uint32_t max_current_300;/* for SD Host Controller spec V3.0 */
    uint32_t max_current_180;/* for SD Host Controller spec V3.0 */

    uint32_t flag;          /* cmddat: cmd & data */
    uint32_t clock;

    uint32_t f_min;
    uint32_t f_max;

    struct mutex mutex;
    thread_cond_t dete_change_cond;
    thread_cond_t dete_change_finish;

    uint32_t max_req_size;  /* maximum number of bytes in one req */
    uint32_t max_blk_size;  /* maximum size of one mmc block */
    uint32_t max_blk_count; /* maximum number of blocks in one req */

    uint32_t claimed:1; /* host exclusively claimed */
    uint32_t sdio_irqs;
    thread_ptr_t *sdio_irq_thread;
    thread_waiter_t waiter;
    int sdio_irq_pending;

    void *card_params;  /* SPL传递的card信息 */
    void (*set_ios)(struct mmc_host *mmc);
    int (*send_cmd_data)(struct mmc_host *mmc, struct mmc_cmd* cmd, struct mmc_data* data);
    int (*get_card_stauts)(struct mmc_host *mmc);
    int (*execute_tuning)(struct mmc_host *mmc, int opcode);  /* for HS200 mode */
    void (*enable_sdio_irq)(struct mmc_host *host, int enable);

    void *private;
};


int mmc_send_cmd_data(struct mmc_host *mmc, struct mmc_cmd* cmd, struct mmc_data* data);
int mmc_send_status(struct mmc_card *card, uint32_t *status);
int mmc_go_idle(struct mmc_host *mmc);
int mmc_execute_tuning(struct mmc_card *card);
void mmc_set_bus_width(struct mmc_host *mmc, uint32_t width);
void mmc_set_clock(struct mmc_host *mmc, uint32_t clock);
void mmc_set_timing(struct mmc_host *mmc, uint32_t timing);
int mmc_set_signal_voltage(struct mmc_host *host, int signal_voltage, uint32_t ocr);
int mmc_select_voltage(struct mmc_host *mmc, uint32_t ocr);

int mmc_core_init(struct mmc_host *mmc);
void mmc_detect_change(struct mmc_host *mmc);

#endif /* __MMC_CORE_H__ */
