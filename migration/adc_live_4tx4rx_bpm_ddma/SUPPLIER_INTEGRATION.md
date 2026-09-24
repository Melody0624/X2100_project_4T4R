# 厂家配置接入（2026-09-10）

## 已完成

- 从厂家 vendor_bsis.c 提取 cheetah_128_512_config，保留寄存器值、顺序、延时、RAM reset 和条件分支。
- supplier_provenance.json 记录原文件和生成表的 SHA256；不修改厂家原文件，也不导入其整套应用或 Flash 参数覆盖机制。
- 默认 93 条操作。显式选择：IS_MIRROR=0、USE_BPM=1、ADC_REPLAY=0、CHEETAH_SAVE=0、SAVE_RAW_DATA=0、USE_USB_OUTPUT=1、UART_ADC_SEND=0。
- 这些是软件暂存选项，不代表已知厂家编译选项。镜像和非镜像 BPM RAM 均保留；不擅自改写算法 bpm_code.h。
- 下发前检查整表的芯片索引、数据长度、特殊命令和延时上限；下发失败返回具体行号并停止后续操作。
- 已对镜像/BPM/raw-output 的 8 种组合增加主机测试，并逐行注入失败。测试回调没有访问真实 SPI。
- 底层仍使用原有 set_regs_to_target/SPI ACK 检查；ACK 不等于寄存器读回一致，更不等于真实 RF 波形验证。未新增未知寄存器的盲目读回。
- 2026-09-23 收到 `bpm_code_512.mat`：变量 `bpm_code` 为 1x512 double，包含
  248 个 -1 和 264 个 +1，与当前 `bpm_code.h` 逐项一致（0 个差异）。同时确认
  硬件最长支持 512 个 code，每帧从 code 0 开始并重新复位。

## 哪些不必等待，哪些必须明确

不必拿到完整寄存器手册才能原样接入和测试厂家下发表。表的结构验证、错误处理、算法回归和诊断框架已经可以推进。

正式接入真实 ADC 前仍需把发射端与接收算法对齐：TX 与 DDMA 子带映射、逐 TX
相位含义、实际 chirp 起始间隔/帧周期和 ADC 数据布局。4TX 全部启用、BPM
512 项序列、每帧 code 0 起点和逐帧复位已经确认。其余内容可以由厂家给出
配置说明或实测确认，不一定非要整本手册。

角度输出还需 TX/RX 阵列坐标、通道顺序和幅相标定。单独验证 SPI/ADC 接收不必先完成测角标定，但本次未改动现有完整流水线的标定保护门。

注意表内条件不同会改变 0x1023/0x1028：普通分支为 0x2E/0x8B680080，raw-output 分支为 0x64/0xF9F80080。不能把所有分支都直接描述为 50.328 ms。注释或分支名称不能代替波形测量。

## 当前安全边界

SUPPLIER_RF_ALGORITHM_CONFIRMED=0，原 RF/阵列门限保持关闭。live_guarded 启动会报告表条数、结构检查结果和 verified=0，然后保持 no RF writes。不要为了跳过提示直接把几个 READY 宏全部改成 1。

这版是“厂家配置已接入的软件候选”，不是“实测完成的 4TX 正式固件”。selftest 仍使用合成 ADC，不能证明这张寄存器表与真实硬件一致。

## 构建与回退

在 WSL 本目录执行 `bash test_host.sh` 或 `SANITIZE=1 bash test_host.sh`。
`bash build_bpm_ddma.sh` 在独立 SDK 副本编译 selftest 和 live_guarded；不修改原始 freertos SDK。
新输出：artifacts/bpm_ddma_4tx4rx_supplier_20260910。
旧输出：artifacts/bpm_ddma_4tx4rx_v3 以及 bpm_ddma_4tx4rx_20260903 保留。
本次没有烧录开发板。
