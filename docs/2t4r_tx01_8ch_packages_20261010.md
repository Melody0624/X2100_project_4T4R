# 两版 2T4R 用户八通道标定烧录包（2026-10-10）

两版均内置用户提供的 TX0/TX1 × RX0～RX3 八通道复数幅相补偿，以 TX0RX0=`1+0i` 为参考。

**B 版实测修正（2026-10-10）：** 原 `B_updated_source/` 包第 2 帧因源码检测窗函数未初始化而崩溃，导致 USB 虚拟串口连接卡住。请改用 `B_updated_source_windowfix/` 内的 `B_updated_source_windowfix_2T4R_TX01_8CH_2M_OTA_20261010.cfg`。修正版 906,124 字节，保留用户八通道系数；实际初始化回归已通过，仍需上板复测。详见 [原因与修复](2t4r_B_windowfix_20261010.md)。

| 版本名称 | 算法来源 | RTOS 大小 | 烧录配置名称 |
| --- | --- | ---: | --- |
| A：厂家新版 2T4R · 用户八通道标定版 | 厂家新版检测编译库 | 926,160 字节 | A_vendor_new_2T4R_TX01_8CH_2M_OTA_20261010.cfg |
| B：更新后源码算法 2T4R · 用户八通道标定版 | 保留检测、CFAR、测角、检测后处理源码，其余采用新版 | 905,180 字节 | B_updated_source_2T4R_TX01_8CH_2M_OTA_20261010.cfg |

## 文件位置

共同目录：`D:/downloads/X2100_project-main/artifacts/2t4r_tx01_8ch_compare_20261010`。

- A 的 cfg、镜像与说明位于 `A_vendor_new/`。
- B 的 cfg、镜像与说明位于 `B_updated_source/`。
- 每个目录都有 `zero_ota.bin`、厂家 OTA SPL、`README_烧录说明.md`、`BUILD_MANIFEST.json`、`SHA256.txt`、`verification.txt`。

## 使用方法

1. 将对应 cfg 放到实际运行的 `cloner.exe` 旁 `configs/x2100/`，重新加载配置或重启烧录工具。
2. 选择名称以 `A_vendor_new` 或 `B_updated_source` 开头的对应配置。
3. 核对 SPL、rtos_0、rtos_1 三项的镜像路径来自同一个版本目录；cfg 已填好绝对路径。
4. 烧录后首次启动使用用户八通道编译默认值，无需另发 `angCalibMat write`。

两版均为 X2100L、2 MB NOR、A/B OTA 布局；`force_erase=1`，会清除原有 Flash 配置和标定。后续仍可用 `angCalibMat write` 保存新系数，重启生效。移动镜像目录后，须修改 cfg 的镜像路径。

两版使用同一厂家预编译 OTA SPL，默认凯越协议。A 版包含校准退出时采样尺寸数组取值的两行修正，B 版此前已修正。角度插值表仍采用各工程默认值。

## 校验结果

- 两版 MIPS 交叉编译完成，均小于单槽 958,464 字节限制。
- 最终 ELF 和 RTOS 中的八通道 float32 系数与用户值一致，两版矩阵逐字节相同。
- 链接记录确认 A 使用厂家检测库，B 直接编译保留的五个检测源码并使用独立速度解模糊对象。
- RTOS 入口、两槽分区、SPL、源码/构建副本与输出 SHA256 校验通过。
- 原 USBCloner spl/img 的七个 bin 哈希保持不变；原 SDK 未因本次独立编译而改写。
- 尚未烧录实测。

复现：`tools/build_2t4r_tx01_compare.sh A_vendor_new` 或 `tools/build_2t4r_tx01_compare.sh B_updated_source`（在 WSL 执行）。
