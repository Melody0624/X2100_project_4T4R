#ifndef _INHERIT_H_
#define _INHERIT_H_

#include <stdint.h>
#include <crc32.h>

#define INHERIT_GLOBAL_VERSION  1 // 管理结构版本

/**
 * @brief 继承初始化
 *
 * @param mem: 指定的配置区起始地址；如果为 NULL 或 size 不足，则自动使用 SHARE_MEM
 * @param size: 配置区大小，仅当 mem 非 NULL 时有效
 * @return: 成功返回 0, 失败返回负数
 */
int inherit_init(void *mem, unsigned int size);


/**
 * @brief 导出设备配置
 *
 * @param magic: 设备标识
 * @param version: 配置结构体版本号
 * @param cfg_data: 配置数据源地址
 * @param cfg_size: 配置数据大小
 * @return: 成功返回 0, 失败返回负数
 */
int inherit_export_cfg(unsigned int magic, unsigned int version, void *cfg_data, unsigned int cfg_size);


/**
 * @brief 从共享内存中动态申请内存
 *
 * @param size: 需要分配的大小
 * @param align: 对齐大小
 * @return: 成功返回内存地址, 失败返回 NULL
 */
void *inherit_malloc(unsigned int size, unsigned int align);

/**
 * @brief 获取共享内存剩余可用字节数
 *
 * @return: 剩余字节数
 */
unsigned int inherit_mem_available(void);

#endif /* _INHERIT_H_ */