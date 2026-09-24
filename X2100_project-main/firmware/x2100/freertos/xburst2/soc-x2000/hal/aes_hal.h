#ifndef _AES_HAL_H_
#define _AES_HAL_H_
#include <types.h>
#include <stdio.h>
#include <driver/aes.h>

enum aes_state {
    aes_IDLE,
    aes_BUSY,
};

enum aes_dece {
    ENCRYPTION,
    DECRYPTION,
};

#define GETU32(pt) (((u32)(pt)[3] << 24) ^ ((u32)(pt)[2] << 16) ^ ((u32)(pt)[1] <<  8) ^ ((u32)(pt)[0]))

/* 设置页面大小, 请设置为默认值3 */
void aes_hal_set_page_size(unsigned int page_size);
/* 获取页面大小, 3:4KB */
unsigned int aes_hal_get_page_size(void);
/* 设置数据输入大小端, 0: 小端, 1: 大端 */
void aes_hal_set_data_input_endian(unsigned int endian);
/* 获取数据输入大小端, 0: 小端, 1: 大端 */
unsigned int aes_hal_get_data_input_endian(void);
/* 清除初始化向量/密钥/输入数据/输出数据 */
void aes_hal_clear_iv_keys(void);
/* 开启DMA编解码 */
void aes_hal_start_dma(void);
/* 使能DMA模式 */
void aes_hal_enable_dma(void);
/* 失能DMA模式 */
void aes_hal_disable_dma(void);
/* 设置密钥长度, 0:128, 1:192, 2:256 */
void aes_hal_set_key_length(enum aes_keyl keyl);
/* 获取密钥长度, 0:128, 1:192, 2:256 */
enum aes_keyl aes_hal_get_key_length(void);
/* 设置编解码是否前后相关模式(cbc), 0:ecb, 1:cbc */
void aes_hal_select_mode(enum aes_mode mode);
/* 获取编解码是否前后相关模式(cbc), 0:ecb, 1:cbc */
enum aes_mode aes_hal_get_mode(void);
/* 设置编解码模式, 0:编码, 1:解码 */
void aes_hal_select_dece(enum aes_dece mode);
/* 获取编解码模式, 0:编码, 1:解码 */
enum aes_dece aes_hal_get_dece(void);
/* 开始编解码 */
void aes_hal_start_encrypt(void);
/* 启动密钥扩展 */
void aes_hal_start_key_expansion(void);
/* 初始化CBC模式所需IV, 表明开始新一轮 */
void aes_hal_initial_iv(void);
/* 使能编解码 */
void aes_hal_enable_encrypt(void);
/* 失能编解码 */
void aes_hal_disable_encrypt(void);
/* 清除dma模式编解码完成标志位 */
void aes_hal_clear_dma_done_status(void);
/* 获取dma模式编解码是否完成标志位 */
unsigned int aes_hal_get_dma_done_status(void);
/* 清除普通编解码完成标志位 */
void aes_hal_clear_aes_done_status(void);
/* 获取普通编解码是否完成标志位 */
unsigned int aes_hal_get_aes_done_status(void);
/* 清除密钥扩展完成标志位 */
void aes_hal_clear_key_expansion_done_status(void);
/* 获取密钥扩展是否完成标志位 */
unsigned int aes_hal_get_key_expansion_done_status(void);
/* 清除全部完成标志位 */
void aes_hal_clear_all_done_status(void);
/* 获取全部完成标志位 */
unsigned int aes_hal_get_all_done_status(void);
/* 使能DMA模式编解码完成中断 */
void aes_hal_enable_dma_done_interrupt(void);
/* 屏蔽DMA模式编解码完成中断 */
void aes_hal_mask_dma_done_interrupt(void);
/* 使能普通模式编解码完成中断 */
void aes_hal_enable_aes_done_interrupt(void);
/* 屏蔽普通模式编解码完成中断 */
void aes_hal_mask_aes_done_interrupt(void);
/* 使能密钥扩展完成中断 */
void aes_hal_enable_key_expansion_done_interrupt(void);
/* 屏蔽密钥扩展完成中断 */
void aes_hal_mask_key_expansion_done_interrupt(void);
/* 使能全部完成中断 */
void aes_hal_enable_all_interrupt(void);
/* 屏蔽全部完成中断 */
void aes_hal_mask_all_interrupt(void);
/* 获取是否使能全部完成中断 */
unsigned int aes_hal_get_all_interrupt(void);
/* 设置DMA模式源物理地址, 传入虚拟地址 */
void aes_hal_set_dma_src_addr(void *virt_addr);
/* 设置DMA模式目标物理地址, 传入虚拟地址 */
void aes_hal_set_dma_dst_addr(void *virt_addr);
/* 设置dma模式传输次数, 每次128位 */
void aes_hal_set_transfer_count(unsigned int blocks);
/* 数据输入, 以128位对齐 */
void aes_hal_data_input(unsigned char *data);
/* 数据输出, 以128位对齐 */
void aes_hal_data_output(unsigned char *data);
/* 设置密钥内容 */
void aes_hal_set_keys(unsigned int *key, enum aes_keyl keyl);
/* 设置初始化向量 */
void aes_hal_set_iv(unsigned char *iv);
/* debug打印寄存器值 */
void aes_dump_regs(void);

void jz_aes_test(void);
#endif /* _AES_HAL_H_ */
