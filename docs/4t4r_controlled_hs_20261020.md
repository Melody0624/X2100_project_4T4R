# 四发四收受控修正版：2026-10-20

当前源码在本分支 `X2100_project-main/firmware/x2100/freertos`。
Windows 工程：`C:/Users/PC/.codex/worktrees/source-4t4r-adaptation/X2100_project-main`。
这是独立四发工程；D:/downloads/X2100_project-main 中的主工程继续保留 B 版两发源码。

对应烧录包：`MT4T4R_HS_CONTROLLED_20261020.cfg`。
本机文件：`D:/downloads/X2100_project-main/artifacts/source4t4r_usb_controlled_20261020/mips`。
RTOS 镜像 912132 字节，SHA256：
`4f7b5ee10688576c214f7002df68b41c316f1c59f9f49a9ea98207eaf66fc101`。
构建时间写入镜像，因此重新编译的镜像哈希可能不同。

源码以实际交付的 V4 ADC 修复包为基线，删除启动观察任务、射频步骤记录、USB 状态打印和端点快照。
保留整包发送互斥与 ADC 缓冲区扩容。USB 允许高速，未强制全速，未增加故障自动降速逻辑。
算法、射频表、16 通道默认补偿、理论角度表、RCS、协议和 256 点/32 航迹限制沿用基线。
四发系数及性能仍需要当前波形下的硬件验证；本次高速受控修正版尚未完成上板稳定性验证。

## 构建

已提交配置模板：`X2100_project-main/firmware/x2100/freertos/configs/x2100_4t4r_controlled_hs_defconfig`。
`tools/build_source4t4r_software.sh` 从模板生成构建配置，并使用工程内的 OTA SPL。
在 WSL Ubuntu 中运行：

```sh
MIPS_TOOLCHAIN_PATH=/path/to/mips-xburst2-newlib430/bin \
BUILD_DIR=/home/your-user/build/source4t4r \
BUILD_OUTPUT=/home/your-user/build/source4t4r-output \
bash tools/build_source4t4r_software.sh
```

默认工具链路径仍指向本机 D 盘厂家工具链目录；可用 MIPS_TOOLCHAIN_PATH 指定。
源码内容校验：`python3 tools/verify_source4t4r_controlled_source.py`。
清单在 `docs/source4t4r_controlled_20261020.json`，文本换行统一为 LF 后比较；生成的链接模板不作为独立源码要求。

## 验证和测试

完整干净 MIPS 编译、100 帧完整 ADC 内容、最大点云包一致性、分配失败和重复初始化的 ASan/UBSan 检查已通过。
全部 2644 个原始构建输入已与 Windows 源码逐一核对。算法、USB 发送互斥和 ADC 内存池目标文件与基线相同。
OTA SPL、两槽、分区、16 通道系数、理论角度、射频表及 USB 数值配置均已核对。

烧录配置 force_erase=1，会清除保存的配置和标定。首次上电默认点云/航迹。
先在同板、同线、同接口验证点云持续至少 5 分钟及关闭重开连接；再单独测试 ADC。
`setRawDataFlg 1` 后重新上电，使用上位机 ADC 接收/记录功能；包长 522276 字节。
`setRawDataFlg 0` 后重新上电恢复点云/航迹。
