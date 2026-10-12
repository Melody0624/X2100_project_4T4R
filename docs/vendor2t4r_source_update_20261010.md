# 厂家旧版 2T4R 工程更新记录（2026-10-10）

## 工程位置与范围

- 已更新工程：`X2100_project-main/firmware/x2100/freertos`。
- 新版来源：`MotorCycle_Ingenic/MotorCycle_Ingenic/freertos`。
- 更新前完整备份：`artifacts/vendor2t4r_source_update_20261010/original_sdk`。
- 保持两发四收；检测、CFAR、测角、检测后处理使用旧版源码，其余 SDK 文件采用新版。

## 旧版与更新后对照

| 部分 | 更新后处理 |
| --- | --- |
| 波形与 RF 配置 | 使用新版 `set_params.c`、Cheetah 配置与控制代码；调频斜率由 19.531 改为 16.699 MHz/μs |
| ADC 采集 | 更新 Cheetah 传感器、CSI/VIC 驱动、采样缓冲区绑定及相机重配置流程 |
| 参数与内存 | 使用新版 `basicParams`、`waveParams` 子帧数组结构和 `MemoryPool`；旧检测代码适配新字段 |
| 检测流水线 | 保留旧源码的去直流、距离 FFT、BPM 解扰、多普勒 FFT、DDMA 解码和 RD 图 |
| CFAR | 保留 `cfar_detection.c`、`cfar_func_v2.c` 的原算法及阈值逻辑 |
| 测角 | 保留旧源码的复数幅相补偿、方位 FFT 和插值 |
| 检测后处理 | 保留旧源码的过滤与坐标处理 |
| 速度解模糊 | 接入新版厂家 CRT 与帧缓存实现；单独提取 `velocity_disambiguation.o`，仍为编译库 |
| 航迹、自车速度、预警 | 使用新版航迹库及新版预警源码/构建选择 |
| 输出与命令 | 使用新版凯越协议、校准请求/应答、USB/UART/CAN 和命令处理代码 |
| 校准 | 使用新版相位校准、初始化校准、通道映射、板型标定与 Flash 配置管理 |
| OTA 与辅助模块 | 使用新版 OTA、SD 更新、网络 ADC 发送、性能日志及其构建配置 |

新版的检测核心只提供编译库，无法逐行确认库内部算法变化。本次没有用该库替换保留的四类源码，因此不承诺检测结果与新版闭源固件完全一致。

## 保留的源码与必要修正

位于 `vendor/motor_cycle_demo/src/detection/`：

1. `detection_processing.c`
2. `cfar_detection.c`
3. `cfar_func_v2.c`
4. `angle_estimation_v1.c`
5. `detection_post_processing.c`

DEBUG 和 RELEASE 均直接编译这些文件，不链接 `libmmw_detection.a`。该厂家归档仍随 SDK 保留，构建只从中提取速度解模糊对象组成 `libmmw_velocity.a`。

适配同时修正了以下问题：

- 距离换算按 512 点 FFT 的 bin 间距计算，避免用 506 点有效采样的物理分辨率换算而引入约 1.19% 比例误差。
- 多普勒索引按 FFT 中点折返为正负速度，适配新版 CRT 输入。
- 补齐新版点结构中的子帧、索引、速度候选与置信度字段。
- 相位校准模式仅分配原始采样缓冲区，检测流程跳过 DSP，避免访问未分配缓冲区。
- 修正新版应用保存校准前 chirp 数、ADC 长度时误把数组地址当整数的问题，改为读取 `[0]`。

每个保留文件的旧版→更新后补丁在 `artifacts/vendor2t4r_source_update_20261010/detection_changes/`。当前源码适配限定为 **2T4R、单子帧、506×128**；切换其他板型或采样模式会通过编译检查明确报错。

## 本次固件默认参数

| 参数 | 数值 |
| --- | --- |
| TX / RX / 虚拟通道 | 2 / 4 / 8 |
| 软件中心频率 | 76.5 GHz |
| ADC 采样率 | 26.665 MS/s |
| 有效采样点 / RX / chirp | 506 |
| chirp 数 / 帧 | 128 |
| chirp 起始间隔 | 26 μs |
| 软件调频斜率 | 16.699 MHz/μs |
| 有效 ADC 采样覆盖的扫频带宽 | 约 316.883 MHz |
| 默认普通帧周期 | 50.328 ms |
| 点云 / 航迹数量上限 | 256 / 32 |
| 默认输出协议 | 凯越协议 |

USB 合并输出 ADC 的新版编译选项仍保留，开启该选项时使用 83.328 ms 分支；本次未启用。

校准默认采用新版 **BARRACADE** 板型表，有效 Flash 标定仍能覆盖默认值。此前用于其他板的 TX0/TX1 八通道系数没有移植到这份固件。安装角、翻转等默认设置也采用新版，实测时需要与上位机和安装方向匹配。

## 编译产物与烧录

目录：`artifacts/vendor2t4r_source_update_20261010/firmware/`。

- `zero_ota.bin`：本次由更新后的源码交叉编译，**905,180 字节**。
- `x2100l-spl-sfc-nor-ota-1200M-500M.bin`：厂家提供的 OTA SPL，24,576 字节；没有 SPL 源码，本次未重新编译 SPL。
- `vendor2t4r_source_2M_ota_20261010.cfg`：独立的 2 MB NOR、A/B OTA 烧录配置，文件路径已指向本目录。
- `BUILD_MANIFEST.json`、`SHA256.txt`、`verification.txt`：来源、哈希与检查结果。

| 分区 | 起点 | 长度 | 写入文件 |
| --- | --- | --- | --- |
| uboot | 0x000000 | 0x6000 | OTA SPL |
| rtos_0 | 0x006000 | 0xEA000 | zero_ota.bin |
| rtos_1 | 0x0F0000 | 0xEA000 | zero_ota.bin |
| boot_flag | 0x1DA000 | 0x1000 | 不单独写入 |
| config | 0x1DB000 | 0x25000 | 不单独写入 |

固件比单槽限制小 53,284 字节。采用 `-Os` 优化以满足 2 MB OTA 分区大小，没有裁掉检测或通信功能。按用户要求，烧录配置 **force_erase=1**，烧录时会清除原有 Flash 配置及标定，之后需要按板子重新配置/校准。默认协议为凯越，须使用能解析该协议的上位机。

原 USBCloner `spl`、`img` 目录的七个 bin 已按先前记录的 SHA256 核对。源码工程根目录的 `zero.bin` 已同步为本次 RTOS 镜像；烧录请使用上述独立 cfg。

## 验证与限制

- MIPS 交叉编译完成，RTOS 入口、镜像边界、堆边界及 OTA 分区容量检查通过。
- 链接命令确认五个检测源文件进入构建，未链接闭源检测库；速度库只有厂家速度解模糊对象。
- 编译副本与当前源码逐文件核对，除换行和编译器路径外保持一致；默认八通道标定已从 ELF 验证。
- 六组合成 ADC 回归使用实际保留的五个源码，覆盖约 18.7–84.1 m、正负速度及 0°/±15°/±30°，并检查后处理与校准绕过；ASan/UBSan 通过。
- 用旧距离换算替换修正后的换算会触发回归失败，恢复后通过。
- 主机测试用可移植 FFT 后端代替 MIPS 指令实现，CRT 用桩函数；它不验证 MIPS FFT 性能、CRT 实测表现或射频链路。
- 编译仍有厂家代码警告，详情见 `build.log`；未宣称零警告。
- 尚未烧录实测，探测距离、精度、处理耗时、校准命令和 OTA 升级仍需板上验证。

复现命令（WSL）：

```bash
bash /mnt/d/downloads/X2100_project-main/tools/build_vendor2t4r_source_update.sh
bash /mnt/d/downloads/X2100_project-main/tools/test_vendor2t4r_source_pipeline.sh
```

`tools/update_vendor2t4r_source.py` 是一次性的原始迁移脚本，会拒绝覆盖已有备份；后续编译无需再次运行它。
