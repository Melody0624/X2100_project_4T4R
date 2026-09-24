#ifndef _OTA_AB_UPGRADE_H_
#define _OTA_AB_UPGRADE_H_

#include <stdint.h>
#include <driver/storage_info.h>
#define OTA_AB_DEBUG        1  //调试日志控制
#define OTA_AB_OBJ_MAX_NUM  3  // 最大支持注册的ota_ab升级对象数
#define OTA_AB_NAME_LEN     64

//启动固件信息
struct ota_ab_start_info {
    char used_partname[OTA_AB_NAME_LEN]; //启动固件使用分区名
    int version; //启动固件ota版本号
    void *userdata; //用户信息
};

// OTA对象信息
struct ota_ab_obj_info {
    char ab_partname[2][OTA_AB_NAME_LEN]; //固件使用的AB分区名
    char target_partname[OTA_AB_NAME_LEN]; //进行升级的目标分区名
    struct ota_ab_start_info start_info; //启动固件信息
};

// OTA对象的操作回调
struct ota_ab_obj_ops {
    /**
     * @brief 固件启动管理回调，不可为NULL
     *
     * 在注册OTA升级对象时被调用，用于管理OTA对象固件启动时使用的固件分区
     *
     * @param ab_partname[2]: OTA对象固件使用的AB分区名
     * @param info: 填充OTA对象的启动固件信息
     *
     * @return: 启动成功返回 0， 启动失败返回 -1
     */
    int (*fw_start_manage)(const char *ab_partname[2], struct ota_ab_start_info *info);

    /**
     * @brief 切换启动分区回调，可为NULL
     *
     * 在OTA升级完成后调用，用于设置系统下次从指定分区启动
     *
     * @param info: OTA对象信息
     *
     * @return: 成功返回 0， 失败返回 -1
     */
    int (*change_start_to_other)(const struct ota_ab_obj_info *info);
};

/**
 * @brief 注册OTA_AB分区升级对象，注册时管理OTA对象的固件启动
 *
 * @param name: OTA对象名称，唯一标识
 * @param ab_partname[2]: OTA对象固件使用的AB分区名
 * @param ops: OTA对象操作回调接口
 * @return: 成功返回 0, 失败返回 -1
 */
int ota_ab_obj_register(const char *name, const char *ab_partname[2], const struct ota_ab_obj_ops *ops);

/**
 * @brief 提供OTA对象信息
 *
 * @param name: OTA对象名称
 * @param info: 传出OTA对象信息
 * @return: 成功返回 0， 失败返回 -1
 */
int ota_ab_obj_get_info(const char *name, struct ota_ab_obj_info *info);

/**
 * @brief 切换OTA对象的下次的固件启动分区
 *
 * @param name: OTA对象名称
 * @return: 成功返回 0, 失败返回 -1
 */
int ota_ab_obj_change_start_to_other(const char *name);

/**
 * @brief 启动OTA对象的OTA升级，设置OTA对象升级状态：未升级 -> 升级中
 *
 * 进行升级写入前调用一次即可，已处于升级中/升级失败状态的OTA对象不可重复调用，
 * 需要stop操作结束上一次升级后才可以重新调用start操作
 *
 * @param name: OTA对象名称
 * @return: 成功返回 0, 失败返回 -1
 */
int ota_ab_obj_upgrade_start(const char *name);


/**
 * @brief 停止OTA对象的OTA升级，设置OTA对象升级状态: 升级中/升级失败 -> 未升级
 *
 * start操作后调用一次即可，处于升级中的ota对象会写入缓冲区剩余的源数据到flash
 * 无论升级成功还是失败，停止升级后OTA对象的升级状态都会被设置为: 未升级
 *
 * @param name: OTA对象名称
 * @return: 升级成功返回已写入的源数据大小, 若本次升级失败或未升级则返回 -1
 */
int ota_ab_obj_upgrade_stop(const char *name);

/**
 * @brief OTA固件数据写入接口，在OTA对象处于升级中状态时，可流式写入源数据到目标分区
 *
 * 可重复调用，直到ota升级的源固件内容都写入完成
 *
 * @param name: OTA对象名称
 * @param data: 指向本次写入目标分区的源数据内容
 * @param len: 本次写入目标分区的源数据大小
 * @return: 成功返回 0, 失败返回 -1
 */
int ota_ab_obj_upgrade_write(const char *name, const unsigned char *data, unsigned int len);

/**
 * @brief 从 Flash 读取数据（安全外部接口）
 * @param from  起始偏移（字节）
 * @param len   读取长度
 * @param buf   输出缓冲区
 * @return      成功返回读取字节数（等于 len），失败返回负值
 */
int ota_ab_flash_read(uint64_t from, uint64_t len, uint8_t *buf);

/**
 * @brief 向 Flash 写入数据（安全外部接口）
 * @param from  起始偏移（字节）
 * @param len   写入长度
 * @param buf   数据缓冲区
 * @return      成功返回写入字节数（等于 len），失败返回负值
 */
int ota_ab_flash_write(uint64_t from, uint64_t len, const uint8_t *buf);

/**
 * @brief 擦除 Flash 区域（安全外部接口）
 * @param addr  起始地址（字节）
 * @param len   擦除长度（字节）
 * @return      成功返回 0，失败返回负值
 */
int ota_ab_flash_erase(uint64_t addr, uint64_t len);

/**
 * @brief 获取分区信息（安全外部接口）
 * @param name   分区名
 * @param offset 输出偏移
 * @param size   输出大小
 * @return       成功返回 0，失败返回负值
 */
int ota_ab_get_part_info(const char *name, uint64_t *offset, uint64_t *size);

/**
 * @brief 获取存储设备信息（安全外部接口）
 * @return 存储信息结构体指针，失败返回 NULL
 */
const struct storage_info *ota_ab_flash_info_get(void);

#endif
