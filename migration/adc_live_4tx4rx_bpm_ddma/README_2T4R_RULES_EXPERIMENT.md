# 4T4R 与 2T4R 检测规则对比实验（2026-09-29）

本版在原 4TX、8 子带、128 chirp 信号处理链上作三项调整，用于排查
“同一 4T4R 板运行 2T4R 固件看得更远”的原因。它不是 2TX 算法的逐位复刻。

1. CFAR 候选不再按 `DDMA_RESOLVED` 状态拒绝；仍计算 8 子带最佳起点并记录
   状态。重叠峰、弱 TX 和污染子带的距离/速度/角度结果可能不可信。
2. 检测点及 USB 点云容量从 16 增到 256。大数组移到静态存储，避免任务栈
   溢出。满载时 AoA 和 USB 包变大，必须在实板测处理时间、丢帧与稳定性。
3. 启动时只读检查 `0x1DB000` 的 RADV 配置。若 Flash 射频表完整且结构有效，
   两次 Cheetah 初始化均使用它；否则使用内置 93 行表，不擦写配置分区。
   UART 会明确打印 `[RF] source=flash rows=N` 或 `source=builtin`。
   如果 Flash 存的是旧 2T4R 84 行表，雷达侧可能只发两路 TX；此时 4TX
   解码模型与实际发射不匹配，不能把输出当作合格的 4T4R 测量。

从本目录在 WSL 中执行：

```bash
OUTPUT_ROOT=/mnt/d/downloads/X2100_project-main/artifacts/mt4t4r_2t4r_rules_20260929 \
  bash build_mt4t4r_supplier_tracker.sh
```

固件与保留配置区的 USBCloner 配置位于
`artifacts/mt4t4r_2t4r_rules_20260929/live_full_experimental/`。
烧录时使用该目录的 `MT-4T4R_live_full_experimental_SFC_NOR_PRESERVE_CONFIG.cfg`，
它设置 `force_erase=0`，不主动清除 `0x1DB000` 配置区。

对比测试需固定同一块板、安装方向、角反/行人路线与场景，保存两版的 UART
启动日志、`[DIAG]`/`[PERF]`/`[HOST]` 行和 USB DAT。特别记录 RF `source` 与
行数；只有射频配置一致时，才能进一步比较算法漏检。当前只完成主机测试与
交叉编译，尚未完成本版实板测试。
