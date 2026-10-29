# B 2T4R：ADC 修正、RCS 初步标定、厂家角度默认值

2026-10-29，配置 `B_2T4R_ADC_RCS_FACTORY_20261029.cfg`。

本包从已确认可工作的 `B_2T4R_ADC_FIX_20261019` 独立构建，保留其 ADC 缓冲区修正、USB、波形与信号处理源码。新增 RCS 模块和帧处理调用，并按用户要求恢复厂家八通道复数幅相补偿、128 项角度映射表。

## 源码与固件位置

- 本包完全对应的 Windows 源码：`D:/downloads/X2100_project-main/artifacts/b_2t4r_adc_rcs_20261029/windows_source/freertos`。
- WSL 构建源码：`/home/melody/Manhattan_Project/freertos_b2t4r_adc_rcs_20261029`。
- 烧录配置与镜像：`D:/downloads/X2100_project-main/artifacts/b_2t4r_adc_rcs_20261029/mips/`。
- Windows 主 B 源码：`X2100_project-main/firmware/x2100/freertos`，同步使用厂家两张默认表，保留 RCS 与 ADC 修正。主目录还包含之前的维护改动，完整冻结源码以第一项为准。
- 四发 worktree 源码与之前的 B 烧录包不变。Cloner `configs/x2100` 新增同名配置，原 `spl`、`img` 文件不覆盖。

原镜像 SHA256 为 `fd3b11f43e94a7aa3e4aa8c6739a561dd3d0544afd0c35eef2f8256cca97adab`。
新镜像 912292 字节，SHA256 为 `10d27f96a08426fd3ede2a6d494a34ae38806f65022f871d76e75c43f775343c`。
来源、改动文件哈希及导出信息保存在 `b2t4r_adc_rcs_factory_20261029.json`，完整构建记录位于烧录目录的 `BUILD_MANIFEST.json`。

## 当前幅相补偿与角度表

两张表来自厂家原始新版 `MotorCycle_Ingenic/MotorCycle_Ingenic/freertos/vendor/motor_cycle_demo/src/board_calib_config.c` 的 `BOARD_TYPE_BARRACADE` 分支；该来源文件 SHA256 为 `f14cf2a0bfe487db294eee9e5317a45574267c03d60de1308128bf4b1d9b2d9e`。

| 通道 | 编译默认补偿 |
|---|---|
| TX0RX0 | −0.252853 − 0.004765i |
| TX0RX1 | −0.227882 − 0.269581i |
| TX0RX2 | −0.179556 − 0.327274i |
| TX0RX3 | −0.385240 − 0.138314i |
| TX1RX0 | −0.336374 + 0.125028i |
| TX1RX1 | −0.300888 − 0.327689i |
| TX1RX2 | −0.137421 − 0.311057i |
| TX1RX3 | −0.241690 − 0.061660i |

`angFFT_interp` 是厂家原有 128 项表，首项 −3.593931°、第二项 0.527261°、末项 −3.943155°。完整数值保存在 `b2t4r_factory_calibration_20261029.json`，全部 float32 值均已与新 ELF 校验。
两张表都是编译默认值；有效 Flash 配置或主动校准结果仍可以覆盖。`force_erase=1` 会清除旧保存配置，使新烧录从厂家默认表启动。阵元几何及校准生成器保持原 B 基线，本次仅替换默认两张表。

## RCS 与 ADC

RCS 默认偏置保留 `−136.526320837 dB`，来自用户 4.6 dBsm 角反记录的 113 帧、雷达估计距离 1.18993322 m。计算为 `pwr + 40*log10(slant_range_m) + offset_db`。该偏置属于先前用户幅相系数下的单距离初步参考；换成厂家幅相系数后需要重新验证或标定，尚未证明新的绝对 RCS 精度。

`rcs_set_reference`、`rcs_clear_calibration`、`rcs_get_frame` 接口保留于源码和目标文件，主流程执行内部逐帧 RCS 计算；当前没有外部标定命令，未调用接口在最终镜像中会被链接器回收。后续接入命令或 CAN 时可调用接口。USB/TLV/CAN 协议未新增 RCS 字段。

ADC 修正保持点云初始化后按需扩容到 522276 字节，分配失败保留原缓冲区，不重新分配 DSP。默认点云/航迹；`setRawDataFlg 1` 后重新上电进入 ADC 输出。

## 验证与复现

独立构建与 Windows 完整导出共 5992 个源码/配置输入一致。相对原 B，仅 `vendor/Makefile`、`vendor/vendor.c`、`board_calib_config.c` 及新增 RCS 头/源文件变化；USB、检测、CFAR、测角、后处理及内存池目标文件与基线相同。
RCS 参考、无效输入、高安装斜距与容量，以及 ADC 100 帧内容、点云包兼容、重复初始化、分配失败、DSP 指针保持的 ASan/UBSan 测试通过。Windows 主源码的厂家默认值、备份/运行期覆盖及 RCS 回归通过。完整 clean MIPS 编译、OTA 双槽/分区/SPL/哈希检查通过；新组合尚未上板测试。

原 B 独立构建目录保留时，可在 WSL 执行：

```sh
bash tools/build_b2t4r_adc_rcs_20261029.sh
python3 tools/package_b2t4r_adc_rcs_20261029.py
```

完整冻结源码与生成镜像在本机 `artifacts/`（该目录按项目规则不纳入 Git）。Git 保存 Windows 主源码更新、厂家完整默认值、回归测试、复现脚本及构建身份记录。原厂完整 SDK 与工具链仍按既有项目布局提供。
