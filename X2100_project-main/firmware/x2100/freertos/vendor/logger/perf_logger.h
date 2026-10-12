#ifndef PERF_LOGGER_H
#define PERF_LOGGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PERF_LOG_DISABLE

// 配置
#ifndef PERF_LOG_BUFFER_SIZE
#define PERF_LOG_BUFFER_SIZE   512     // 环形缓冲区条目数
#endif

#ifndef PERF_LOG_BATCH_SIZE
#define PERF_LOG_BATCH_SIZE    16      // 用于 flush 时批量打印，限制单次输出长度
#endif

// 条件编译
#ifdef PERF_LOG_DISABLE
    #define perf_logger_init()                    ((void)0)
    #define perf_logger_record(tag, dur, src, depth) ((void)0)
    #define perf_logger_printf(format, ...)       ((void)0)
    #define perf_logger_flush()                   ((void)0)
    #define perf_logger_get_time_ms()             (0.0f)
    #define perf_logger_get_discard_count()       (0U)
    #define PERF_BEGIN(tag)                       do{}while(0)
    #define PERF_END(tag, src)                    do{}while(0)
    #define PERF_END_DEPTH(tag, src, depth)       do{}while(0)
#else
    /**
     * @brief 初始化性能日志系统
     */
    void perf_logger_init(void);

    /**
     * @brief 记录性能数据（只入队，不打印）
     * @param tag        标签字符串
     * @param duration_ms 耗时（毫秒）
     * @param source      来源标识：0=CPU0，非0=CPUx
     * @param depth       缩进层级，0=顶层，1=子阶段，2=子子阶段等
     */
    void perf_logger_record(const char *tag, float duration_ms, uint8_t source, uint8_t depth);

    /**
     * @brief 异步普通打印（仍入队，类型为 PRINT）
     * @param format 格式化字符串
     * @param ...    可变参数
     */
    void perf_logger_printf(const char *format, ...);

    /**
     * @brief 异步刷新打印队列
     */
    void perf_logger_flush(void);

    /**
     * @brief 获取当前时间（毫秒）
     */
    float perf_logger_get_time_ms(void);

    /**
     * @brief 获取丢弃计数
     */
    uint32_t perf_logger_get_discard_count(void);

    // ==================== 耗时测量宏（带标签） ====================
    /**
     * @brief 开始测量（tag 必须是合法的 C 标识符，如 nci、ddma，不能带引号）
     *        宏会自动生成变量 _perf_start_##tag
     */
    #define PERF_BEGIN(tag) \
        float _perf_start_##tag = perf_logger_get_time_ms()

    /**
     * @brief 结束测量并记录（显式指定源，默认深度0（顶层））
     *        tag 与 PERF_BEGIN 中的 tag 一致，宏内部自动转换为字符串
     * @param src 来源标识：0=CPU0，非0=CPUx
     */
    #define PERF_END(tag, src) \
        perf_logger_record(#tag, (float)(perf_logger_get_time_ms() - _perf_start_##tag), src, 0)

    /**
     * @brief 结束测量并记录（显式指定源和深度，如子阶段 depth=1）
     * @param src 来源标识：0=CPU0，非0=CPUx
     * @param depth 缩进层级，0=顶层，1=子阶段，2=子子阶段等
     */
    #define PERF_END_DEPTH(tag, src, depth) \
        perf_logger_record(#tag, (float)(perf_logger_get_time_ms() - _perf_start_##tag), src, depth)

#endif // PERF_LOG_DISABLE

#ifdef __cplusplus
}
#endif

#endif // PERF_LOGGER_H
