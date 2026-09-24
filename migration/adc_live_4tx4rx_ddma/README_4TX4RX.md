# X2100 4TX4RX 软件框架版

此目录是从已验证的 Cheetah 2TX4RX 实时 ADC 版本复制出的独立版本，不会覆盖：

- `migration/adc_live`：现有 2TX4RX 实时采集版；
- `artifacts/live_adc_20260826`：现有 2TX4RX 烧录产物；
- `artifacts/replay_20260826`：无雷达前端的回放版。

## 已完成的软件改造

- 参数化为 4 TX、4 RX、16 个虚拟通道；
- DDMA 检测图改为 8 个总子带、每子带 16 个 Doppler bin；
- TX 子带映射独立封装，不再由算法假设 `start + tx`；
- 发射编码、帧周期、chirp 周期和中心频率集中到独立配置接口；
- 16 路虚拟通道映射和 16 路复数校准独立封装；
- AoA 输入扩展为 16 路，继续使用 128 点角度 FFT；
- 保留 MotorCycle Tools USB 输出协议和现有实时采集框架；
- 增加编译期/启动期配置检查及醒目的占位配置提示。

## 尚未具备的硬件信息

以下内容不能根据“增加两根发射天线”可靠推断，因此本版本明确使用占位值：

- 4TX Cheetah 寄存器表和发射使能时序；
- 实际 BPM/DDMA 编码和子带间隔；
- 16 个虚拟阵元的物理顺序、间距和重复阵元处理；
- 16 路幅相校准系数；
- 4TX 帧周期、chirp 周期及速度分辨率。

当前 `radar_frontend.c` 仍包含旧 2TX Cheetah 寄存器表；默认软件假设为 8 个
总 DDMA 子带，TX 偏移为 0、1、2、3；阵元按 TX-major 排列，校准系数全部为
`1 + 0j`，角度轴暂按半波长 ULA 计算。因此该版本可以用于编译、接口联调和
内存/性能检查，但在补齐前端资料之前，其实测目标、速度和角度不能作为有效结果。

## 构建

在 WSL 中执行：

```bash
cd /mnt/d/downloads/X2100_project-main/migration/adc_live_4tx4rx
chmod +x install_and_build_4tx4rx_framework.sh
./install_and_build_4tx4rx_framework.sh
```

产物输出到：

`D:/downloads/X2100_project-main/artifacts/live_adc_4tx4rx_20260827`

烧录前必须确认串口日志包含 `[4T4R]`，并理解其中的 `WARNING` 表示射频前端
尚不是 4TX 实配版本。
