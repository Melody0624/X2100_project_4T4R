#ifndef OTA_TRIGGER_AB_H
#define OTA_TRIGGER_AB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief OTA 传输通道类型
     *
     * 由调用方（USB CDC 或 UART3 接收任务）根据接收命令的通道传入，
     * ota_cmd_handler 据此选择对应的传输接口实例。
     */
    typedef enum {
        OTA_TRANSPORT_USB = 0,   /**< USB CDC 虚拟串口 */
        OTA_TRANSPORT_UART = 1,  /**< UART3 物理串口 */
    } ota_transport_t;

    /**
     * @brief 初始化 OTA 触发模块（在 main 中调用）
     *
     * 本模块不再创建独立的 USB 监听任务，而是通过 CLI 命令分发处理升级请求。
     * USB CDC 数据由 vendor/gadget_serial 接收后经 CliSiganlProcess() 路由。
     */
    void ota_trigger_ab_init(void);

    /**
     * @brief CLI 命令 "otaUpgrade" 的处理函数
     *
     * 注册在 vendor/uart_cli/cli.c 的 CliSiganlProcess() 命令链中，
     * 同时在 UART3 接收任务（uart_trans.c）中直接调用。
     *
     * 收到升级命令后启动 OTA 框架，升级目标为 A/B 双区中当前非活动分区（inactive），
     * 升级成功后切换 BootFlag 槽位并跳转到新固件。
     *
     * transport 参数由调用方传入，标识是哪个通道收到了命令：
     *   - USB CDC 接收任务传入 OTA_TRANSPORT_USB
     *   - UART3 接收任务传入 OTA_TRANSPORT_UART
     *
     * 两种通讯方式的传输回调、任务挂起/恢复函数被封装为统一的
     * ota_transport_iface_t 结构体实例，由本函数根据 transport 选择后
     * 传入升级线程，实现解耦。
     *
     * @param argc      参数个数
     * @param argv      参数字符串数组
     * @param transport 命令来源的传输通道（USB / UART3）
     * @return 0 表示命令已处理，-1 表示出错
     */
    int32_t ota_cmd_handler(int32_t argc, char *argv[], ota_transport_t transport);

    /**
     * @brief 检查 OTA 升级是否正在运行
     *
     * 用于 CliSiganlProcess 在字节处理循环中检查 OTA 是否已启动，
     * 如果已启动则停止处理剩余的数据，避免二进制 OTA 协议数据被
     * 当作 CLI 命令解析而导致 "Unknown command" 错误。
     *
     * @return 0 未运行，1 正在运行
     */
    int ota_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_TRIGGER_AB_H */
