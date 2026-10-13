# B 版 2T4R：RCS 计算与标定接口

本改动只作用于保留检测、CFAR、测角、后处理源码的工程：
`X2100_project-main/firmware/x2100/freertos`。不增加 USB/TLV 或 CAN 字段，不修改检测/跟踪库使用的 `DetObj`、`GlbCtx` 布局。

## 计算定义

源码 `compute_rd_map()` 将各 TX/RX 的复数幅度汇总后取 `20*log10`，CFAR 和测角把该值保存在 `DetObj.pwr`。
计算使用这个浮点原值，不能使用报文中乘以 100、转换为 uint16 的值。

对同一波形、增益、通道设置下的远场点目标：

```text
relative_db = pwr_db + 40*log10(slant_range_m / 1 m)
offset_db   = reference_dbsm - reference_pwr_db - 40*log10(reference_range_m / 1 m)
dbsm        = relative_db + offset_db
```

距离补偿来自单站雷达方程的回波功率与 R^4 成反比关系：
[MathWorks Radar Equation](https://www.mathworks.com/help/radar/ug/radar-equation.html)。
本工程汇总的是幅度，补偿为 40*log10(R)，不能使用 20*log10(R)。
高空安装模式下 `rng` 已转换为地面投影距离，模块会根据 `mountingHeightSqr` 恢复斜距再计算。

2026-10-13 起，B 版 2T4R 启动默认使用角反初步标定偏置 **−136.526320837 dB**：用户角反 4.6 dBsm，记录 `Record_20261013_111210.dat` 的 113 帧参考点，雷达估计平均距离 1.18993322 m。该距离未经独立测量，标定需在其他距离复核。仅在 `BOARD_ANTENNA_CONFIG=BOARD_ANTENNA_2T4R` 时启用，不适用于新的 4T4R 波形。

调用 `rcs_clear_calibration()` 后，有效点的 `relative_db` 可用，`dbsm=NAN`、`calibrated=false`。
相对值没有 dBsm 意义，不应显示为真实散射截面积。
非法距离或非有限强度使 `valid=false`。不按零值判定有效性，0 dBsm 本身是合法值。

## 标定接口

```c
#include "rcs_estimation.h"

/* 示例数据仅演示接口，不是本板实测标定值。
 * 已知角反射器 10 dBsm、斜距 10 m、测得原始 pwr 为 100 dB： */
bool ok = rcs_set_reference(10.0f, 10.0f, 100.0f);

/* 无效输入返回 false，并保留上一次有效标定。 */
/* 取消标定、清空内部结果： */
rcs_clear_calibration();
```

实际标定应使用已知 RCS 的角反射器，放在远场、正对雷达，保持波形、接收增益和处理参数不变，选择其对应点的稳定回波强度（可先做多帧平均）。参考距离为斜距，单位米；参考 RCS 为 dBsm。
运行中的标定常数保存在 RAM；重启恢复上述编译默认值。重新标定或清除仅在本次运行中生效；没有增加 Flash 参数、CLI 命令或协议命令。以后可由 CAN 配置处理调用此接口。
不要把通道幅相补偿系数当作绝对 RCS 标定常数。

这是通过单点参考标定得到的 RCS 估计。当前没有天线方向图补偿、噪声功率扣除、饱和识别或频率相关的幅度校正；多径、低 SNR、非正向目标和增益/波形变化会引入误差。标定后仍需实测验证，改变波形/增益/通道设置后应重新标定。

## 每帧结果读取

`vendor.c` 在最终点云排序、过滤和跟踪处理完成后、封包前调用：

```c
rcs_process_frame(ctx, pool_index);
const RcsFrame *frame = rcs_get_frame(pool_index);
```

- `frame_id` 对应 `ctx->frmInfo.frmID`。
- `count` 对应最终 `ctx->detInfo.numDets`，最大 256。
- `points[i]` 对应同一帧最终点云的 `detObj[i]`。
- `valid` 表示输入和相对计算有效；`calibrated` 表示该点可以读取 `dbsm`。
- 标定变更、取消标定和正常停止采样会清空已有结果；无目标的新帧 `count=0`。

未来 CAN 发送示例：

```c
const RcsFrame *frame = rcs_get_frame(pool_index);
if (frame && frame->frame_id == ctx->frmInfo.frmID) {
    for (unsigned int i = 0; i < frame->count; ++i) {
        const RcsEstimate *rcs = &frame->points[i];
        if (rcs->valid && rcs->calibrated) {
            /* 按未来 CAN 协议将 rcs->dbsm 编码为带符号数。 */
        }
    }
}
```

所有 RCS API 应在帧处理任务中调用。结果是内部静态存储，下一帧会覆盖；异步 CAN 发送任务需要在生产者任务中复制同一帧点云及 RCS，然后通过队列发送。
此接口提供逐检测点 RCS，不提供航迹 RCS 聚合。

## 验证

- `tools/test_rcs_estimation.sh`：实际模块的参考目标、距离翻倍、异常输入、高空斜距、256 点边界和帧清理测试，启用 ASan/UBSan。
- `tools/build_2t4r_rcs_internal.sh`：独立 MIPS 编译，并运行真实初始化及 6 个 ADC→检测→CFAR→测角→后处理→RCS 合成用例和原有阵列校准用例。
- 构建目录与此前 A/B 固件目录分开；构建脚本只编译验证，`tools/package_2t4r_rcs_reference.py` 另行生成初步标定烧录包。复现命令见 `docs/2t4r_rcs_reference_20261013.md`。
- 已取得上述单距离角反记录，完成启动默认值和接口回归；尚未进行独立距离、多距离及上板 RCS 精度验证。
