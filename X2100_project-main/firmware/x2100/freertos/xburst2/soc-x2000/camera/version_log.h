/*
 * Copyright (C) 2020 Ingenic Semiconductor Co., Ltd.
 *
 * version log
 *
 */
#ifndef __VERSION_LOG_H__
#define __VERSION_LOG_H__


/*
 * tisp core version
 */
#define TISP_CORE_VERSION               "1.03"


/*
 * isp驱动版本号说明：
 * 格式：x.y.z
 *   x：当驱动有较大结构性变动时更新
 *   y：当添加新功能或删除已有功能时更新
 *   z：没有功能变化但是内部实现有更新时更新
 * 注：当大版本更新时，次一级版本清零
 *
 * 日志记录：
 *     当x.y有变化时记录日志
 * 日志格式：
 *     版本号：
 *         功能说明日志
 *
 */
#define DRIVER_VERSION                  "0.1.0"


/**
     ---------- camera驱动日志 ----------
     [Camera, ISP, CIM, VIC, mscaler]

0.1.0:
    [Camera]isp出图

1.0.0：
    [ISP]isp图像效果生效

1.1.0：
    [Camera]添加调试节点，打印sensor，vic，isp，mscaler信息，抓取raw图

1.2.0:
    [ISP]关闭iq bin版本检查

1.3.0:
    [Camera]调整sensor格式的宏定义

2.0.0:
    [Camera]目录结果调整

2.1.0:
    [ISP]isp tuning支持多进程操作
 */





#endif /* __VERSION_LOG_H__ */
