#ifndef RTOS_OTA_INIT_H
#define RTOS_OTA_INIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 初始化 RTOS OTA 升级对象
     *
     * 在系统启动时调用，完成：
     *   1. 注册 RTOS 为 OTA 升级对象
     *   2. 启动标志检测由 rtos_ota_init 内部回调自动完成
     */
    int rtos_ota_init(void);

#ifdef __cplusplus
}
#endif
#endif /* RTOS_OTA_INIT_H */
