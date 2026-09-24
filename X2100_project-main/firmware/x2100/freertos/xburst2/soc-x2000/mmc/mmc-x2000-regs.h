/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */
#ifndef __MMC_X2000_REGS_H__
#define __MMC_X2000_REGS_H__

#include <soc/base.h>

/*
 * MMC/SD Controller Registers Map
 */
#define MSC_SDMA_ADDRESS                0x000
#define MSC_BLOCK_SIZE                  0x004
#define MSC_BLOCK_COUNT                 0x006
#define MSC_ARGUMENT                    0x008
#define MSC_TRANSFER_MODE               0x00C
#define MSC_COMMAND                     0x00E
#define MSC_RESPONSE_01                 0x010
#define MSC_RESPONSE_23                 0x014
#define MSC_RESPONSE_45                 0x018
#define MSC_RESPONSE_67                 0x01C
#define MSC_BUFFER_DATA                 0x020
#define MSC_PRESENT_STATE               0x024
#define MSC_HOST_CONTROL                0x028
#define MSC_POWER_CONTROL               0x029
#define MSC_BLOCK_GAP_CONTROL           0x02A
#define MSC_WAKE_UP_CONTROL             0x02B  /* 未列出 */
#define MSC_CLOCK_CONTROL               0x02C
#define MSC_TIMEOUT_CONTROL             0x02E
#define MSC_SOFTWARE_RESET              0x02F
#define MSC_NORMAL_INT_STATUS           0x030
#define MSC_ERROR_INT_STATUS            0x032
#define MSC_NORMAL_INT_ENABLE           0x034
#define MSC_ERROR_INT_ENABLE            0x036
#define MSC_NORMAL_INT_SIGNAL_ENABLE    0x038
#define MSC_ERROR_INT_SIGNAL_ENABLE     0x03A
#define MSC_AUTO_CMD12_ERROR            0x03C
#define MSC_HOST_CONTROL2               0x03E
#define MSC_CAPABILITIES                0x040
#define MSC_CAPABILITIES_1              0x044
#define MSC_MAX_CURRENT                 0x048
#define MSC_FORCE_AUTO_CMD12_STAT       0x050
#define MSC_FORCE_ERROR_INT_STAT        0x052
#define MSC_ADMA_ERROR_STAT             0x054
#define MSC_ADMA_ADDRESS_LOW            0x058
#define MSC_ADMA_ADDRESS_HIGH           0x05C

/* 0x60 ~ 0xFB reserved */
#define MSC_PRESET_INIT                 0x060
#define MSC_PRESET_DS                   0x062
#define MSC_PRESET_HS                   0x064
#define MSC_PRESET_SDR12                0x066
#define MSC_PRESET_SDR25                0x068
#define MSC_PRESET_SDR50                0x06A
#define MSC_PRESET_SDR104               0x06C
#define MSC_PRESET_DDR50                0x06E
#define MSC_PRESET_HS400                0x074  /* Non-standard */
#define MSC_ADMA_ID_LOW                 0x078
#define MSC_ADMA_ID_HIGH                0x07C

#define MSC_SLOT_INT_STATUS             0x0FC
#define MSC_HOST_VERSION                0x0FE

/* command queuing */
#define MSC_CMD_QUEUING_OFFSET          (0x180)
#define MSC_CQ_VERSION                  (MSC_CMD_QUEUING_OFFSET + 0x00)
#define MSC_CQ_CAPABILITIES             (MSC_CMD_QUEUING_OFFSET + 0x04)
#define MSC_CQ_CONFIGURATION            (MSC_CMD_QUEUING_OFFSET + 0x08)
#define MSC_CQ_CONTROL                  (MSC_CMD_QUEUING_OFFSET + 0x0C)
#define MSC_CQ_INT_STATUS               (MSC_CMD_QUEUING_OFFSET + 0x10)
#define MSC_CQ_INT_ENABLE               (MSC_CMD_QUEUING_OFFSET + 0x14)
#define MSC_CQ_INT_SIGNAL_ENABLE        (MSC_CMD_QUEUING_OFFSET + 0x18)
#define MSC_CQ_INT_COALESCING           (MSC_CMD_QUEUING_OFFSET + 0x1C)
#define MSC_CQ_TASK_DESC_BASE_ADDR      (MSC_CMD_QUEUING_OFFSET + 0x20)
#define MSC_CQ_TASK_DESC_BASE_ADDR_UPPER (MSC_CMD_QUEUING_OFFSET + 0x24)
#define MSC_CQ_DOOR_BELL                (MSC_CMD_QUEUING_OFFSET + 0x28)
#define MSC_CQ_TASK_CLEAR_NOTIFICATION  (MSC_CMD_QUEUING_OFFSET + 0x2C)
#define MSC_CQ_DEVICE_QUEUE_STATUS      (MSC_CMD_QUEUING_OFFSET + 0x30)
#define MSC_CQ_DEVICE_PENDING_TASK      (MSC_CMD_QUEUING_OFFSET + 0x34)
#define MSC_CQ_TASK_CLEAR               (MSC_CMD_QUEUING_OFFSET + 0x38)
#define MSC_CQ_SEND_STATUS_CONFIG_1     (MSC_CMD_QUEUING_OFFSET + 0x40)
#define MSC_CQ_SEND_STATUS_CONFIG_2     (MSC_CMD_QUEUING_OFFSET + 0x44)
#define MSC_CQ_DIRECT_CMD_RESP          (MSC_CMD_QUEUING_OFFSET + 0x48)
#define MSC_CQ_CMD_RESP_ERROR_MASK      (MSC_CMD_QUEUING_OFFSET + 0x50)
#define MSC_CQ_TASK_ERROR_INFOMATION    (MSC_CMD_QUEUING_OFFSET + 0x54)
#define MSC_CQ_CMD_RESPONSE_INDEX       (MSC_CMD_QUEUING_OFFSET + 0x58)
#define MSC_CQ_CMD_RESPONSE_ARG         (MSC_CMD_QUEUING_OFFSET + 0x5C)

#define MSC_EMMC_EMMC_CONTROL           (0x500 + 0x2C)

/*
 * block size  ( MSC_BLOCK_SIZE [0x004] )
 */
/* Host SDMA buffer boundary */
#define MSC_DEFAULT_BOUNDARY_ARG        (7)
#define MSC_DEFAULT_BOUNDARY_SIZE       (512 * 1024)


#define MSC_BLOCK_sdma_buffer_boundary  12,14
#define MSC_BLOCK_xfer_block_size       0,11

/*
 * transfer mode  ( MSC_TRANSFER_MODE [0x00C] )
 */
#define MSC_TRANSFER_response_interrupt     8,8
#define MSC_TRANSFER_response_err_check     7,7
#define MSC_TRANSFER_response_type          6,6
#define MSC_TRANSFER_multi_single_select    5,5
#define MSC_TRANSFER_data_direction         4,4
#define MSC_TRANSFER_auto_cmd_enable        2,3
#define MSC_TRANSFER_block_count_enable     1,1
#define MSC_TRANSFER_dma_enable             0,0

/*
 * command  ( MSC_COMMAND [0x00E] )
 */
#define MSC_COMMAND_cmd_index           8,13
#define MSC_COMMAND_cmd_type            6,7
#define MSC_COMMAND_data_presend_sel    5,5
#define MSC_COMMAND_cmd_index_crc_check 4,4
#define MSC_COMMAND_cmd_crc_check       3,3
#define MSC_COMMAND_response_type       0,1

/*
 * presend state Register  ( MSC_PRESENT_STATE [0x024] )
 */
#define MSC_PRESENT_AVAILABLE_READ      (1 << 11)  /* non-DMA模式 读状态, fifo有有效数据 */
#define MSC_PRESENT_AVAILABLE_WRITE     (1 << 10)  /* non-DMA模式 写状态, fifo有空间可继续写 */
#define MSC_PRESENT_DOING_READ          (1 << 9)
#define MSC_PRESENT_DOING_WRITE         (1 << 8)
#define MSC_PRESENT_DATA_INHIBIT        (1 << 1)
#define MSC_PRESENT_CMD_INHIBIT         (1 << 0)

/*
 * host control  ( MSC_HOST_CONTROL [0x028] )
 */
typedef enum {
    DMA_MODE_SDMA           = 0,
    DMA_MODE_RESERVED       = 1,
    DMA_MODE_ADMA2          = 2,
    DMA_MODE_ADMA3          = 3,
} msc_dma_mode;

#define MSC_HOST_CTRL_ext_transfer_width    5,5
#define MSC_HOST_CTRL_dma_select            3,4
#define MSC_HOST_CTRL_high_speed_enable     2,2
#define MSC_HOST_CTRL_data_transfer_width   1,1

/*
 * power control  ( MSC_POWER_CONTROL [0x029] )
 */
#define MSC_POWER_CTRL_POWER_OFF        (0x00)
#define MSC_POWER_CTRL_POWER_ON         (0x01)
#define MSC_POWER_CTRL_POWER_180        (0x0A)
#define MSC_POWER_CTRL_POWER_300        (0x0C)
#define MSC_POWER_CTRL_POWER_330        (0x0E)

/*
 * block gap control  ( MSC_BLOCK_GAP_CONTROL [0x02A] )
 */
#define MSC_BLOCK_GAP_int_block_gap     3,3
#define MSC_BLOCK_GAP_read_wait_ctrl    2,2
#define MSC_BLOCK_GAP_continue_request  1,1
#define MSC_BLOCK_GAP_stop_request      0,0

/*
 * clock control  ( MSC_CLOCK_CONTROL [0x02C] )
 */
#define MSC_CLOCK_control_mask          2,3
#define MSC_CLOCK_control_init_stable   1,1
#define MSC_CLOCK_control_init_enable   0,0
/*
 * software reset  ( MSC_SOFTWARE_RESET [0x02F] )
 */
#define MSC_SOFTWARE_reset_cmd_data     1,2
#define MSC_SOFTWARE_reset_all          0,0


/*
 * normal interrupt status  ( MSC_NORMAL_INT_STATUS [0x030] )
 */
#define MSC_NORMAL_transfer_complete    1,1
#define MSC_NORMAL_response_complete    0,0

#define MSC_NORMAL_INT_ERROR            (1 << 15)
#define MSC_NORMAL_INT_CMD_QUEUING_EVENT    (1 << 14) /* 不知有何用途 */
#define MSC_NORMAL_INT_FX_EVENT         (1 << 13) /* 不知有何用途 */
#define MSC_NORMAL_INT_RE_TUNING_EVENT  (1 << 12)
#define MSC_NORMAL_INT_CARD_INT         (1 << 8)  /* Card中断 */
#define MSC_NORMAL_INT_READ_READY       (1 << 5)
#define MSC_NORMAL_INT_WRITE_READY      (1 << 4)
#define MSC_NORMAL_INT_DMA              (1 << 3)
#define MSC_NORMAL_INT_BGAP_EVENT       (1 << 2)
#define MSC_NORMAL_INT_XFER_COMPLETE    (1 << 1)
#define MSC_NORMAL_INT_CMD_COMPLETE     (1 << 0)

/*
 * error interrupt status  ( MSC_ERROR_INT_STATUS [0x032] )
 */
#define MSC_ERROR_INT_RESPONSE_ERR      (1 << 11)
#define MSC_ERROR_INT_TUNING_ERR        (1 << 10)
#define MSC_ERROR_INT_ADMA_ERR          (1 << 9)
#define MSC_ERROR_INT_AUTO_CMD_ERR      (1 << 8)
#define MSC_ERROR_INT_BUS_POWER_ERR     (1 << 7)
#define MSC_ERROR_INT_DATA_END_BIT_ERR  (1 << 6)
#define MSC_ERROR_INT_DATA_CRC_ERR      (1 << 5)
#define MSC_ERROR_INT_DATA_TIMEOUT_ERR  (1 << 4)
#define MSC_ERROR_INT_CMD_INDEX_ERR     (1 << 3)
#define MSC_ERROR_INT_CMD_END_BIT_ERR   (1 << 2)
#define MSC_ERROR_INT_CMD_CRC_ERR       (1 << 1)
#define MSC_ERROR_INT_CMD_TIMEOUT_ERR   (1 << 0)

#define MSC_ERROR_STATUS_CMD_ERROR      (MSC_ERROR_INT_CMD_TIMEOUT_ERR | \
                                    MSC_ERROR_INT_CMD_CRC_ERR           | \
                                    MSC_ERROR_INT_CMD_END_BIT_ERR       | \
                                    MSC_ERROR_INT_CMD_INDEX_ERR         | \
                                    MSC_ERROR_INT_AUTO_CMD_ERR          | \
                                    MSC_ERROR_INT_RESPONSE_ERR)

#define MSC_ERROR_STATUS_DATA_ERROR     (MSC_ERROR_INT_DATA_TIMEOUT_ERR | \
                                    MSC_ERROR_INT_DATA_CRC_ERR          | \
                                    MSC_ERROR_INT_DATA_END_BIT_ERR      | \
                                    MSC_ERROR_INT_ADMA_ERR)

#define MSC_ERROR_STATUS_ALL_ERROR      (MSC_ERROR_STATUS_CMD_ERROR | MSC_ERROR_STATUS_DATA_ERROR)

/*
 * normal && error status二者寄存器通过32bit的操作一并读出
 */
#define MSC_NORMAL_REG_OFFSET           0
#define MSC_ERROR_REG_OFFSET            16
/* command mask */
#define MSC_NORMAL_ERROR_CMD_MASK       (MSC_NORMAL_INT_CMD_COMPLETE << MSC_NORMAL_REG_OFFSET   | \
                                    MSC_ERROR_INT_CMD_TIMEOUT_ERR    << MSC_ERROR_REG_OFFSET    | \
                                    MSC_ERROR_INT_CMD_CRC_ERR        << MSC_ERROR_REG_OFFSET    | \
                                    MSC_ERROR_INT_CMD_END_BIT_ERR    << MSC_ERROR_REG_OFFSET    | \
                                    MSC_ERROR_INT_CMD_INDEX_ERR      << MSC_ERROR_REG_OFFSET)

/* data mask */
#define MSC_NORMAL_ERROR_DATA_MASK      (MSC_NORMAL_INT_XFER_COMPLETE << MSC_NORMAL_REG_OFFSET  | \
                                    MSC_NORMAL_INT_BGAP_EVENT         << MSC_NORMAL_REG_OFFSET  | \
                                    MSC_NORMAL_INT_DMA                << MSC_NORMAL_REG_OFFSET  | \
                                    MSC_NORMAL_INT_WRITE_READY        << MSC_NORMAL_REG_OFFSET  | \
                                    MSC_NORMAL_INT_READ_READY         << MSC_NORMAL_REG_OFFSET  | \
                                    MSC_ERROR_INT_DATA_TIMEOUT_ERR    << MSC_ERROR_REG_OFFSET   | \
                                    MSC_ERROR_INT_DATA_CRC_ERR        << MSC_ERROR_REG_OFFSET   | \
                                    MSC_ERROR_INT_DATA_END_BIT_ERR    << MSC_ERROR_REG_OFFSET   | \
                                    MSC_ERROR_INT_ADMA_ERR            << MSC_ERROR_REG_OFFSET)

/*
 * host control 2  ( MSC_HOST_CONTROL2 [0x03E] )
 */
#define MSC_CTRL_UHS_SDR12              0
#define MSC_CTRL_UHS_SDR25              1
#define MSC_CTRL_UHS_SDR50              2
#define MSC_CTRL_UHS_SDR104             3
#define MSC_CTRL_UHS_DDR50              4

#define MSC_CTRL_EMMC_LEGACY            0
#define MSC_CTRL_EMMC_HS_SDR            1   /* High Speed SDR */
#define MSC_CTRL_EMMC_Resverd0          2
#define MSC_CTRL_EMMC_HS200             3
#define MSC_CTRL_EMMC_HS_DDR            4   /* High Speed DDR */
#define MSC_CTRL_EMMC_Resverd1          5
#define MSC_CTRL_EMMC_Resverd2          6
#define MSC_CTRL_EMMC_HS400             7   /* Non standard */

#define MSC_HOST_CTRL_2_tuning_status   7,7
#define MSC_HOST_CTRL_2_exec_tuning     6,6
#define MSC_HOST_CTRL_2_tuned_clk       3,3
#define MSC_HOST_CTRL_2_uhs_mode        0,2

/*
 * host capabilities  ( MSC_CAPABILITIES [0x040] )
 */
#define MSC_HOST_CAPABILI_max_blk_len   16,17

/*
 * max current  ( MSC_MAX_CURRENT [0x048] )
 */
#define MSC_MAX_CURRENT_vol_330         0,7
#define MSC_MAX_CURRENT_vol_300         8,15
#define MSC_MAX_CURRENT_vol_180         16,23


/*
 * host version  ( MSC_HOST_VERSION [0x0FE] )
 */
#define MSC_HOST_VERSION_vendor         8,15
#define MSC_HOST_VERSION_spec           0,7

//#define SDHCI_SPEC_100                  (0)
//#define SDHCI_SPEC_200                  (1)
//#define SDHCI_SPEC_300                  (2)

#endif /* __MMC_X2000_REGS_H__ */
