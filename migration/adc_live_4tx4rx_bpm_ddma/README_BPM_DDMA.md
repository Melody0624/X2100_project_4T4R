# X2100 4TX4RX：公共 BPM + DDMA 软件候选 v3

CAN 协议软件层见 [CAN_PROTOCOL.md](CAN_PROTOCOL.md)。当前构建输出为
`artifacts/bpm_ddma_4tx4rx_can_20260910`，CAN 实际传输尚未绑定，USB 输出保留。

2026-09-10：已接入厂家 `cheetah_128_512_config` 条件分支，详见
[SUPPLIER_INTEGRATION.md](SUPPLIER_INTEGRATION.md)。当前构建输出为
`artifacts/bpm_ddma_4tx4rx_supplier_20260910`；以下 v3 产物保留，不被覆盖。

最新配置、诊断接口、连续150帧自测及限制见 [OPTIMIZATION_V3.md](OPTIMIZATION_V3.md)。
参数入口已移至 radar_config.h。新产物位于 artifacts/bpm_ddma_4tx4rx_v3；
以下 v2 修改记录用于说明算法基线。

本目录独立于 `adc_live_4tx4rx_ddma_opt`，没有覆盖旧源码、旧固件或旧回放版。
实现基础是原 Cheetah C 工程及其已移植的 X2100 处理链，不是 TI SDK，也不是
MATLAB 生成代码。TI 文档仅用于核对 DDMA 通用原理。

## 固定参数与处理顺序

- 4 TX、4 RX、16 虚拟通道，128 chirp，26 us chirp 起始间隔。
- 506 个有效 ADC 样本/RX/chirp，26.665 MS/s，19.531 MHz/us，76.5 GHz。
- 原始 DMA payload = 128 * (32 字节头 + 506 * 4 * 2) = 522240 字节。
- ADC 为小端、12 位 offset-binary、左移 4 位；沿用原驱动的布局，不能直接
  当成有符号 int16 数据。32 字节 chirp 头暂不能校验语义，尚缺厂商字段资料。
- DC 去除/原 Blackman -> 512 点 RFFT -> 原公共 BPM 去扰码/原 Hanning ->
  128 点 DFFT -> 8 子带 DDMA -> 原 Doppler CASO/Range CAGO CFAR -> AoA。
- TX 相位增量候选 0/45/90/135 度/chirp，公共 BPM 0/180 度叠加在全部 TX 上。
  厂家提供的 `bpm_code_512.mat` 与 `bpm_code.h` 的 512 项逐项一致；硬件最长支持
  512 个 code，每帧从 code 0 开始并复位。本配置每帧 128 chirp，因此使用
  码表的第 0～127 项。该确认不等于 TX 与 DDMA 子带映射已经确认。
- 每子带 16 个频率格，不是每个 TX 只有 16 个慢时间样本。
- Doppler 格点 0.588770 m/s；原始采样区间约 +/-37.6813 m/s。
  后者不是已验证的 DDMA 无模糊测速范围，也不是性能验收结果。
- 目标帧周期 50328 us；需要在 X2100 真机上测试吞吐、最坏耗时和 USB 丢帧。

## v2 的实质修改

1. 新 `ddma_resolver.c` 枚举全部八个子带起点，保留候选位图、最佳/次佳分数，
   检查弱 TX、候选接近、空子带污染、NaN/Inf；不再在平分时直接相信第一个。
2. 不确定峰不进入速度/测角/点云输出，串口统计 rejected_peaks、候选位图等。
   被拒绝位置的能量仍用于 CFAR 背景估计，避免置零造成边缘假峰。
3. 距离格点修正为 c*Fs/(2*S*512)=0.399703411 m。506 点采样带宽对应的
   c/(2B)=0.404442977 m 是物理分辨率，不能拿来乘 512 FFT 的 bin。
4. 保留一份就地 Range/Doppler cube（实际 1 MiB）及常驻工作缓冲区，不重复
   做 Range FFT。新候选判定增加约 80 KiB 状态空间，以 sizeof 实测打印为准。
5. ADC 接口要求精确 payload 长度，拒绝非法低四位及超过 1% 样本饱和的帧。
   1% 是工程初值，并非硬件验收阈值。失败帧发空点云，防止上位机残留旧点。
6. 点云 rel_rd_index 不再全是零；计数上限仍为 16，超过会统计 overflow。
7. 默认不运行旧摩托车近地面/倍频反射经验过滤，终点仍为检测和 AoA。
   保留原显示坐标旋转 180 度、+/-60 度视场及 0.5 m 近距限制。
   2026-09-22 已在 `radar_tracking.c`、`radar_warning.c` 增加可审查的跟踪和
   预警链路，见 `TRACKING_WARNING.md`；多雷达融合仍未实现。
8. 真实前端采取禁止运行保护：缺少确认的 4TX 寄存器表/阵列校准时，
   `radar_frontend_init()` 在 SPI 写寄存器前返回错误，不把旧 2TX 表当 4TX 表发送。

## 必须理解的限制

这不是通用的多目标解模糊解算器。当前波形和幅度模板不能保证任意重叠场景
唯一可辨识；保守门限也不能保证识别所有混叠。相同距离、相同折叠 Doppler
位置的重叠目标可能被拒绝。因此不能声称满足“所有多目标无漏检”要求。

例如同距离、同方向、等强度，速度 4.1214 和 13.5417 m/s 的测试会被拒绝，
不会伪装成已经解出两个可靠目标。要在这类情况下仍保证两个目标都输出，
需进一步设计多假设联合估计、利用可验证的空间信息或改变编码/跨帧波形。

其他不同距离，或同距离但折叠 Doppler 位置充分分离的双目标已有合成测试。
幅度比/空带对比默认门限在 `ddma_default_policy`，需用真实 ADC 标定。
`quality` 只在诊断中表示相对分数，不是概率；未擅自将其塞入上位机既有置信度
字段。上位机 wire layout 保持兼容；当前产品联调构建的 CDC line coding 为
460800，USB 数据实际通过 bulk 传输。

2026-09-23：实际TX/RX坐标与用户确认的从左到右芯片通道编号已接入，
形成间距1.96 mm的16通道ULA，角度轴按实际间距与波长计算。见ANTENNA_GEOMETRY.md。
幅相校准仍为1+0j，DDMA子带对应关系仍待验证。合成角度误差只验证数学实现，
不代表新板实测精度；ARRAY_CALIBRATION_READY仍为0。

## 两个固件

### selftest（现在可以用于没有雷达前端的 X2100 板）

运行真正的 ADC 解包/完整 C 算法，输入为启动时生成的合成 ADC，而不是写死
四个点。信号含一个 +25 m/s、约 20.085 m、+15 度的理想目标和少量噪声。
启动首帧会数值检查；v3随后逐帧生成移动/空场景/双目标距离交叉序列。
可关闭RADAR_SELFTEST_SEQUENCE恢复同帧基线；两种方式均没有证明CSI采集链。

预期串口标记（UART2 应用日志速率以构建清单为准；当前联调构建为115200、8N1，
预置 SPL 的最早期日志仍为3000000）：

```text
[SIM] SYNTHETIC ADC ONLY - NOT real radar measurements
[SIM] full ADC pipeline check PASS ...
[PERF] ...
[DDMA] last_frame ...
[HOST] connected=... produced=... sent=... dropped=... errors=...
```

USB_DOWNLOAD 接电脑，用 MotorCycle Tools 2.4.1 选择新 COM，460800。
点云、航迹和预警均为合成测试结果，不是现场探测结果；前三帧航迹尚未成熟，
随后才输出 track。预警是否出现取决于合成目标是否进入工程初值区域。
注意不确定的低能量背景单元可能很多，应看 rejected_peaks 和有效目标结果，
不能把 ambiguous_cells 的数量直接当作目标数量。

### live_guarded（预留真实采集版本，当前会明确停止）

启动打印缺少确认资料并停止雷达任务，不会偷偷切换成模拟输入。
接入真实前端必须补齐：

1. 厂商确认的逐 chirp 四路 TX DDMA 相位/子带映射及 ADC/CSI 配置；四路 TX 启用、
   公共 BPM 512 项码表、每帧 code 0 起点和逐帧复位已经确认。
2. 实现 `radar_rf_profile.c` 的 ready/apply/readback 流程，替换当前失败占位实现。
3. 填入真实虚拟天线顺序/位置、16 路复数校准；当前 FFT 测角只适用于对应 ULA。
4. 确认后再修改 RF/array ready 标志，并完成静止目标、动态与多目标实测。

## 测试和构建（在 WSL）

```bash
cd /mnt/d/downloads/X2100_project-main/migration/adc_live_4tx4rx_bpm_ddma
bash test_host.sh
SANITIZE=1 bash test_host.sh
bash build_bpm_ddma.sh
```

测试使用实际 `vendor.c`；仅在主机上把 NE10/MXU FFT 替换成可移植 FFT，
替换 RTOS/USB 接口。不会启动硬件或运行 MATLAB。覆盖速度扫描、边界、双目标、
强弱目标、噪声、错误长度、低位错误、ADC 饱和及重复运行恢复。
主机测试结果不是 MIPS SIMD 数值、板端实时性或射频验收结果。
当前 222 个完整 C 处理链用例通过；最坏速度误差约 0.290 m/s，模拟距离误差
约 0.00261 m，理想阵列角度误差约 0.00552 度。这些只描述合成用例，不能当作
硬件精度指标。错误公共 BPM 起点的负例没有恢复出正确目标，但不代表算法
能自动识别所有码相位错误；实际对齐仍需厂商确认。
AddressSanitizer/UBSan 测试使用非 PIE 主机程序并关闭递归 SIGSEGV 报告器，
内存访问插桩仍开启，原生致命信号仍使测试失败；这是本机 WSL 检测工具兼容处理。

构建脚本在 `/home/melody/Manhattan_Project/freertos_bpm_ddma_20260903` 建立
独立 SDK 副本（约 344 MiB），保留 `/home/melody/Manhattan_Project/freertos`
原工作目录。脚本不烧录，不连接或控制开发板。

产物：`artifacts/bpm_ddma_4tx4rx_v3/selftest/` 和 `live_guarded/`。
每个目录有 `rtos-with-spl.bin`、`zero.bin`、`zero.elf`、匹配的 Cloner cfg、
SHA256、构建清单。烧录前仍需核对板卡 NAND/LPDDR2 和备份；不要把 Linux、NOR
或其他硬件的 cfg 混用。烧录的是二进制镜像，不是 defconfig。

本版没有在开发板上实测；请以构建日志、主机测试日志和后续串口记录分别判断。
