#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

/* 最大允许的单项波形配置大小，对应雷达配置解析缓冲 (预留40KB) */
#define MAX_CHEETAH_CFG_SIZE 40960

typedef enum {
    PARAM_SERIAL_NUMBER = 0,
    PARAM_PRODUCTION_DATE,
    PARAM_ANG_CALIB_MAT,
    PARAM_ANG_FFT_CFG,
    PARAM_CHEETAH_DEFAULT_CFG,
    PARAM_CHEETAH_FFT_COMP_CFG,
    PARAM_CHEETAH_CALIB_CFG,
    PARAM_CHEETAH_DOT_CFG,
    PARAM_RADAR_INSTALL_ANG,
    PARAM_IS_CALIB_INSTALL_ANG,
    PARAM_IS_FLIP_INSTALL_AZM,
    PARAM_IS_IN_HIGH_ALTITUDE,
    PARAM_CALIB_TARGET_DIST,
    PARAM_CALIB_TARGET_ANG,
    PARAM_CALIB_POWER_TH,
    PARAM_CALIB_TARGET_VEL,
    PARAM_CALIB_RESULT_ANG,
    PARAM_CALIB_RESULT,
    PARAM_RADAR_STATUS,
    PARAM_MAX_FRAME_CNT,
    PARAM_OUT_RAW_DATA_FLG,
    PARAM_MAX_COUNT,
} ParamID;

/**
 * @brief 初始化配置管理器，如果配置文件不存在则自动创建默认的二进制框架结构，并写入初始的雷达配置
 * @param default_cheetah_cfg 初始的雷达默认配置数组指针
 * @param cheetah_cfg_len 初始雷达配置的总字节数
 */
int config_manager_init(const void *default_cheetah_cfg, uint32_t cheetah_cfg_len, float def_radarInstallAng, uint8_t def_isCalibInstallAng, uint8_t def_isInhighAltitude, uint8_t def_isFlipInstall_azm);

/**
 * @brief 根据ID直接从文件偏移读取参数，不占用系统全局RAM
 * @param id  参数的ID
 * @param buf 用户栈上的接收缓冲
 * @param len 需要读取的长度
 * @return 0成功，-1失败
 */
int param_get(ParamID id, void *buf, uint32_t len);

/**
 * @brief 根据ID直接将参数写入文件指定偏移，实现原子化单项更新
 * @param id  参数的ID
 * @param buf 待写入的数据
 * @param len 写入的长度
 * @return 0成功，-1失败
 */
int param_set(ParamID id, const void *buf, uint32_t len);

#endif /* CONFIG_MANAGER_H */