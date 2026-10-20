# B 源码版 4T4R：软件适配阶段

基线为 GitHub 提交 `710a53b`。代码位于独立分支 `codex/source-4t4r-adaptation`，工作区：

`C:/Users/PC/.codex/worktrees/source-4t4r-adaptation/X2100_project-main`

主目录 `D:/downloads/X2100_project-main` 中的 B 版 2T4R SDK 和已有烧录包保持原样。

## 已完成

| 参数 | 四发配置 |
| --- | --- |
| TX / RX / 虚拟通道 | 4 / 4 / 16 |
| 采样率 / 有效点 | 26.665 MS/s / 506 |
| 中心频率 / 斜率 | 76.5 GHz / 16.699 MHz/μs |
| chirp 数 / 间隔 / 帧周期 | 128 / 26 μs / 50.328 ms |
| DDMA 子带 / 每带 bin | 8 / 16 |
| TX 位移 | 0、16、32、48 Doppler bin，即 0、45、90、135°/chirp |
| 虚拟阵列 | TX-major/RX-minor；右到左；−1.96 mm 有符号间距 |
| FFT 点数 | 距离 512；Doppler 128；方位 128 |
| 点云 / 航迹上限 | 256 / 32 |
| 校准矩阵 | 16 复数，Flash 保存 32 个 float |

补齐全部 16 项通道/测角索引和阵元位置，扩展方位窗长度、启动矩阵读取、零相候选/保存及 USB 校准命令。
默认使用此前 `Record_20260928_202805_adc.dat`、range bin 5 的 16 通道系数（来自旧四发工程的数据表）；新的波形下尚未硬件复核。
默认角度表为负间距的 128 项理论映射，保留现有主动校准成功覆盖默认值的流程。

四发 Flash 布局使用不同 magic `0x52414434`；两发仍为 `0x52414456`。
四发矩阵接口要求完整 128 字节，避免把八通道短矩阵当作完整四发矩阵。
USB `angCalibMat write` 现在要求 32 个 float，排列为 TX0RX0.real/imag 到 TX3RX3.real/imag；`read` 返回 16 行。
正常启动会按原配置管理流程识别布局；因此已有八通道配置不能直接作为四发配置继承。

## 算法与协议

检测/DDMA 流程没有替换；`detection_processing.c` 仅放宽原来的两发编译限制，允许两发或四发的 506×128 配置。
CFAR、测角、后处理、自车速度接口、RCS、零相补偿计算和角度表生成算法文件与基线一致；厂家库文件没有更换。
CFAR 窗/门限、动静分类及反射点门限保持原值，实测再复核适用性。
输出协议文件和字段保持原样，RCS 仍仅为内部结果与标定接口。
RCS 在四发下需要新的绝对幅度参考标定；当前默认没有有效 dBsm 标定。

## 已验证

- 基线两发初始化与全流程回归通过。
- 四发 22 个合成 ADC 全流程用例通过，包括全部 8 个 DDMA 子带边界及正负速度/角度。
- 四发 16 通道零相和角度校准、保存失败回退、最后一个通道失效拒绝保存通过。
- 真实配置管理模块：完整矩阵读写、相邻字段不受损、旧布局区分测试通过。
- 真实 USB 矩阵命令处理函数：32float 写入、16 行读出、缺项/多项拒绝通过。
- 两发 6 个全流程用例和旧 Flash 布局回归通过；原 RCS 用例通过。
- 实际 MIPS 对比确认 GlbCtx、DetObj、DetObjRD、MemoryPool、Mmw_pkt_info 大小及关键偏移在两发/四发间相同。
- MIPS 编译和完整算法链接通过；ELF 中默认矩阵、阵元位置、理论角度表逐项校验通过。
- 主机测试使用 ASan/UBSan，FFT 硬件后端由可移植 FFT 替代；这些测试不能替代实际射频和 MIPS SIMD 的硬件测量。

测试和诊断构建记录：
`D:/downloads/X2100_project-main/artifacts/source4t4r_adaptation_20261012`

## 普通寄存器表已接入，等待硬件验证

2026-10-14 已根据用户指定的 `vendor_bsis(2).c` 接入正常四发表，并将 FSTEP 从 0xC8 改为 0xAB。实际斜率为 16.69921875 MHz/μs；其余字段与原表一致。详见 `docs/4t4r_rf_slope_20261014.md`。
2026-10-14 随后按用户要求生成实验上板包，当前 `SOURCE_4T4R_RF_READY=1`，启动进入正常射频和实时采样初始化。
新的独立诊断构建位于 `D:/downloads/X2100_project-main/artifacts/source4t4r_rf_slope_20261014/mips`，`zero_software_only.bin` 为 911764 字节。MIPS 编译及实际链接的 93 行寄存器表逐字校验通过。该镜像用于编译、链接和容量检查，不是最终上板包；尚未生成四发烧录 cfg。
构建使用先前已验证的外部 `.config.in` 和 OTA SPL，其来源与哈希保存在 `BASELINE_BUILD_INPUTS.json`。原烧录工具 spl/img 未覆盖。

仍需上板核对 TX 使能、采样率、实际斜率、chirp/帧时序、共同 BPM 序列和起始复位、DDMA 位移及 MIPI 帧格式。
硬件移相器校准等专用 RF 配置也需核对；当前只确认阵列校准软件维度和处理流程，未宣称所有硬件校准表已适配。
实验烧录包已生成：`D:/downloads/X2100_project-main/artifacts/source4t4r_live_20261014`，配置 `MT4T4R_Source_16p699_16CH_256P32T_OTA_20261014.cfg`。RTOS 911860 字节，单槽剩余 46604 字节，`force_erase=1`。
硬件移相器校准参数已按四发适配：mask=0xF、8 子带、独立相位数 1/8/4/8；粗扫/细扫专用寄存器表继续使用 19.53125 MHz/μs、53.33 MS/s，正常工作收敛检查采用新表和 16.69921875 MHz/μs。
MIPS 编译、实际 ELF 的启动开关/完整 RF 表、22 个全流程用例、16 通道阵列校准、硬件校准参数与两发兼容性测试、OTA 双槽分区和源码/镜像哈希验证通过。正式探测性能仍需上板验证连续采样、距离、速度、左右方向、点云、航迹和校准。

## 验证入口

```sh
# 默认四发全流程；TEST_TWO_TX=1 切换两发兼容性回归
bash tools/test_vendor2t4r_source_pipeline.sh
bash tools/test_4t4r_config_layout.sh
python3 tools/test_4t4r_calibration_io.py artifacts/cli
bash tools/test_4t4r_shared_abi.sh
bash tools/test_rcs_estimation.sh
python3 tools/test_4t4r_rf_slope.py
bash tools/build_source4t4r_software.sh
```

MIPS 工具链可由 `MIPS_TOOLCHAIN_PATH` 指定；缺少忽略的本地构建配置和 SPL 时，`BASELINE_PROJECT` 指向保留的两发主目录。无需修改当前 2T4R 源码。
