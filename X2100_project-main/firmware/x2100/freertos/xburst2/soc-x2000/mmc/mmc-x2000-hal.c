/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */

#include "mmc-x2000-regs.h"
#include "mmc-x2000-hal.h"

/*
 * Controller Function API
 */
void mmc_hal_set_sdma_address(int index, uint32_t addr)
{
    msc_write_reg_32(index, MSC_SDMA_ADDRESS, addr);
}


void mmc_hal_set_block_size(int index, uint16_t value)
{
    msc_set_bits_16(index, MSC_BLOCK_SIZE, MSC_BLOCK_xfer_block_size, value);
}

void mmc_hal_set_sdma_buffer_boundary(int index, uint16_t value)
{
    msc_set_bits_16(index, MSC_BLOCK_SIZE, MSC_BLOCK_sdma_buffer_boundary, value);
}


void mmc_hal_set_block_count(int index, uint16_t value)
{
    msc_write_reg_16(index, MSC_BLOCK_COUNT, value);
}


void mmc_hal_set_argument(int index, uint32_t value)
{
    msc_write_reg_32(index, MSC_ARGUMENT, value);
}


uint16_t mmc_hal_get_transfer_mode(int index)
{
    return msc_read_reg_16(index, MSC_TRANSFER_MODE);
}

void mmc_hal_set_transfer_mode(int index, uint16_t mode)
{
    msc_write_reg_16(index, MSC_TRANSFER_MODE, mode);
}

/* for HS200 Exce Tuning read status */
void mmc_hal_set_data_transfer_direction_read(int index)
{
    msc_set_bits_16(index, MSC_TRANSFER_MODE, MSC_TRANSFER_data_direction, 1);
}


void mmc_hal_set_command(int index, uint16_t value)
{
    msc_write_reg_16(index, MSC_COMMAND, value);
}


uint32_t mmc_hal_read_bufferdata(int index)
{
    return msc_read_reg_32(index, MSC_BUFFER_DATA);
}

void mmc_hal_write_bufferdata(int index, uint32_t value)
{
    msc_write_reg_32(index, MSC_BUFFER_DATA, value);
}


int mmc_hal_get_presend_status(int index)
{
    return msc_read_reg_32(index ,MSC_PRESENT_STATE);
}


/*
 * 配置1/4bit bus width
 * =0, 1bit bus width
 * =1, 4bit bus width
 */
void mmc_hal_set_transfer_width_enable_4bit(int index, int enable)
{
    msc_set_bits_8(index, MSC_HOST_CONTROL, MSC_HOST_CTRL_data_transfer_width, enable);
}

/*
 * 是否使能8bit bus width
 * =0, 1/4bit bus width(由 data_transfer_width 决定)
 * =1, 8bit bus width
 */
void mmc_hal_set_transfer_width_enable_8bit(int index, int enable)
{
    msc_set_bits_8(index, MSC_HOST_CONTROL, MSC_HOST_CTRL_ext_transfer_width, enable);
}

void mmc_hal_set_dma_mode(int index, msc_dma_mode mode)
{
    msc_set_bits_8(index, MSC_HOST_CONTROL, MSC_HOST_CTRL_dma_select, mode);
}

void mmc_hal_set_high_speed_enable(int index, int enable)
{
    msc_set_bits_8(index, MSC_HOST_CONTROL, MSC_HOST_CTRL_high_speed_enable, enable);
}


void mmc_hal_set_power_control(int index, uint8_t value)
{
    msc_write_reg_8(index, MSC_POWER_CONTROL, value);
}


void mmc_hal_block_gap_request_stop(int index)
{
    msc_set_bits_16(index, MSC_BLOCK_GAP_CONTROL, MSC_BLOCK_GAP_stop_request, 0x1);
}

void mmc_hal_block_gap_request_start(int index)
{
    msc_set_bits_16(index, MSC_BLOCK_GAP_CONTROL, MSC_BLOCK_GAP_stop_request, 0x0);
}


void mmc_hal_clock_control_enable(int index, uint32_t enable)
{
    if (enable)
        msc_set_bits_16(index, MSC_CLOCK_CONTROL, MSC_CLOCK_control_mask, 0x3);
    else
        msc_set_bits_16(index, MSC_CLOCK_CONTROL, MSC_CLOCK_control_mask, 0x0);
}

int mmc_hal_clock_control_initialization_stable(int index)
{
    return msc_get_bits_16(index, MSC_CLOCK_CONTROL, MSC_CLOCK_control_init_stable);
}

/*
 * 1. MSC CPM配置正常工作， clock_gate打开，
 * 2. 在将MSC控制器的CLOCK_CONTROL, bit[0]置位
 * 3. 检查MSC CPM的busy是否被清0
 * 如果不按上述步骤，MSC CPM的busy会一直被置位，不会清为0
 */
void mmc_hal_clock_control_initialization_enable(int index, uint16_t enable)
{
    if (enable)
        msc_set_bits_16(index, MSC_CLOCK_CONTROL, MSC_CLOCK_control_init_enable, 0x1);
    else
        msc_set_bits_16(index, MSC_CLOCK_CONTROL, MSC_CLOCK_control_init_enable, 0x0);
}


/*
 * value:unit MSC clock cycle
 * range: 0~0xE (MSC_CLK * 2^13   ~  MSC_CLK * 2^27)
 * defalut:0x0
 */
void mmc_hal_set_data_timeout(int index, uint8_t value)
{
    msc_write_reg_8(index, MSC_TIMEOUT_CONTROL, value & 0xE);
}


void mmc_hal_software_reset_controller(int index)
{
    msc_set_bits_8(index ,MSC_SOFTWARE_RESET, MSC_SOFTWARE_reset_all, 1);
}

void mmc_hal_software_reset_cmd_data(int index)
{
    msc_set_bits_8(index ,MSC_SOFTWARE_RESET, MSC_SOFTWARE_reset_cmd_data, 3);
}

int mmc_hal_software_is_resetting_controller(int index)
{
    return msc_get_bits_8(index, MSC_SOFTWARE_RESET, MSC_SOFTWARE_reset_all);
}

int mmc_hal_software_is_resetting_cmd_data(int index)
{
    return msc_get_bits_8(index ,MSC_SOFTWARE_RESET, MSC_SOFTWARE_reset_cmd_data);
}

/*
 * MSC_NORMAL_INT_STATUS状态寄存器, 对应的位是写1清0， 写0忽略，
 * 不用读取检查，将对应的位 置位
 */
void mmc_hal_clear_int_flag_transfer_complete(int index)
{
    msc_write_reg_16(index, MSC_NORMAL_INT_STATUS, MSC_NORMAL_INT_XFER_COMPLETE);
}

int mmc_hal_is_transfer_complete(int index)
{
    return msc_get_bits_16(index ,MSC_NORMAL_INT_STATUS, MSC_NORMAL_transfer_complete);
}

void mmc_hal_clear_int_flag_response_complete(int index)
{
    msc_write_reg_16(index, MSC_NORMAL_INT_STATUS, MSC_NORMAL_INT_CMD_COMPLETE);
}

int mmc_hal_is_response_complete(int index)
{
    return msc_get_bits_16(index ,MSC_NORMAL_INT_STATUS, MSC_NORMAL_response_complete);
}

int mmc_hal_get_normal_int_status(int index)
{
    return msc_read_reg_16(index ,MSC_NORMAL_INT_STATUS);
}

void mmc_hal_clear_normal_int_status(int index, uint16_t value)
{
    msc_write_reg_16(index, MSC_NORMAL_INT_STATUS, value);
}

int mmc_hal_get_error_int_status(int index)
{
    return msc_read_reg_16(index ,MSC_ERROR_INT_STATUS);
}

void mmc_hal_clear_error_int_status(int index, uint16_t value)
{
    msc_write_reg_16(index ,MSC_ERROR_INT_STATUS, value);
}

/* 该接口同时获取normal/error 状态寄存器的值 */
uint32_t mmc_hal_get_all_int_status(int index)
{
    return msc_read_reg_32(index ,MSC_NORMAL_INT_STATUS);
}

/* 该接口同时写入normal/error 状态寄存器的值 */
void mmc_hal_clear_all_int_status(int index, uint32_t value)
{
    msc_write_reg_32(index, MSC_NORMAL_INT_STATUS, value);
}


void mmc_hal_enable_normal_interrupt(int index, uint16_t value)
{
    msc_write_reg_16(index, MSC_NORMAL_INT_ENABLE, value);
}

uint16_t mmc_hal_get_normal_interrupt_status(int index)
{
    return msc_read_reg_16(index, MSC_NORMAL_INT_ENABLE);
}


void mmc_hal_enable_error_interrupt(int index, uint16_t value)
{
    msc_write_reg_16(index, MSC_ERROR_INT_ENABLE, value);
}

uint16_t mmc_hal_get_error_interrupt_status(int index)
{
    return msc_read_reg_16(index, MSC_ERROR_INT_ENABLE);
}


void mmc_hal_enable_normal_interrupt_signal(int index, uint16_t value)
{
    msc_write_reg_16(index, MSC_NORMAL_INT_SIGNAL_ENABLE, value);
}

uint16_t mmc_hal_get_normal_interrupt_signal_status(int index)
{
    return msc_read_reg_16(index, MSC_NORMAL_INT_SIGNAL_ENABLE);
}


void mmc_hal_enable_error_interrupt_signal(int index, uint16_t value)
{
    msc_write_reg_16(index, MSC_ERROR_INT_SIGNAL_ENABLE, value);
}

uint16_t mmc_hal_get_error_interrupt_signal_status(int index)
{
    return msc_read_reg_16(index, MSC_ERROR_INT_SIGNAL_ENABLE);
}


int mmc_hal_get_exec_tuning(int index)
{
    return msc_get_bits_16(index, MSC_HOST_CONTROL2, MSC_HOST_CTRL_2_exec_tuning);
}

void mmc_hal_set_exec_tuning(int index)
{
    msc_set_bits_16(index, MSC_HOST_CONTROL2, MSC_HOST_CTRL_2_exec_tuning, 1);
}

void mmc_hal_set_uhs_signaling(int index, int value)
{
    msc_set_bits_16(index, MSC_HOST_CONTROL2, MSC_HOST_CTRL_2_uhs_mode, value);
}

uint32_t mmc_hal_get_host_capablites_max_block_length(int index)
{
    int value = msc_get_bits_32(index, MSC_CAPABILITIES, MSC_HOST_CAPABILI_max_blk_len);

    return 512 << value;
}


int mmc_hal_get_max_current_330(int index)
{
    return msc_get_bits_32(index, MSC_MAX_CURRENT, MSC_MAX_CURRENT_vol_330);
}

int mmc_hal_get_max_current_300(int index)
{
    return msc_get_bits_32(index, MSC_MAX_CURRENT, MSC_MAX_CURRENT_vol_300);
}

int mmc_hal_get_max_current_180(int index)
{
    return msc_get_bits_32(index, MSC_MAX_CURRENT, MSC_MAX_CURRENT_vol_180);
}

int mmc_hal_get_host_vendor_version(int index)
{
    return msc_get_bits_16(index ,MSC_HOST_VERSION, MSC_HOST_VERSION_vendor);
}

int mmc_hal_get_host_sepc_version(int index)
{
    return msc_get_bits_16(index ,MSC_HOST_VERSION, MSC_HOST_VERSION_spec);
}

