# 4T4R 与 2T4R 检测规则对比实验（2026-09-30 修正）

本版在原 4TX、8 子带、128 chirp 信号处理链上作三项调整，用于排查
“同一 4T4R 板运行 2T4R 固件看得更远”的原因。它不是 2TX 算法的逐位复刻。

1. CFAR 候选不再按 `DDMA_RESOLVED` 状态拒绝；仍计算 8 子带最佳起点并记录
   状态。重叠峰、弱 TX 和污染子带的距离/速度/角度结果可能不可信。
2. 检测点及 USB 点云容量从 16 增到 256。大数组移到静态存储，避免任务栈
   溢出。满载时 AoA 和 USB 包变大，必须在实板测处理时间、丢帧与稳定性。
3. 每次启动时核对 `0x1DB000` 的 RADV 配置中的射频表。如果与固件内置的
   93 行 4T4R 表一致，直接使用 Flash，不重复擦写；如果是有效但不同的
   旧表（例如 2T4R 的 84 行表），仅将射频表字段更新为固件表，读回验证
   后再使用。配置分区完全空白时先建立 RADV 魔数，再写射频表。其他字段
   （序列号、幅相校准、安装角等）由 `param_set` 按扇区读改写保留。
   UART 会显示 `sync=already_current` 或 `sync=updated_from_firmware`。
   遇到非空且损坏的表、未知配置格式或 Flash 读写验证失败时，为避免
   覆盖可能存在的其他数据，使用固件内置表并打印原因，不自动修复。
   **有意修改 Flash 射频表的调参会被下一次启动恢复为固件内置版本；**
   因此要更改射频表，应更新源码、编译并烧录新固件。更新过程不是断电
   原子操作，首次迁移前应备份配置区并保持供电稳定。

从本目录在 WSL 中执行：

```bash
OUTPUT_ROOT=/mnt/d/downloads/X2100_project-main/artifacts/mt4t4r_rf_sync_20260930 \
  bash build_mt4t4r_supplier_tracker.sh
```

固件与保留配置区的 USBCloner 配置位于
`artifacts/mt4t4r_rf_sync_20260930/live_full_experimental/`。
烧录时使用该目录的 `MT-4T4R_live_full_experimental_SFC_NOR_PRESERVE_CONFIG.cfg`，
它设置 `force_erase=0`，不主动清除 `0x1DB000` 配置区。

对比测试需固定同一块板、安装方向、角反/行人路线与场景，保存两版的 UART
启动日志、`[DIAG]`/`[PERF]`/`[HOST]` 行和 USB DAT。特别记录 RF `source` 与
行数；只有射频配置一致时，才能进一步比较算法漏检。当前只完成主机测试与
交叉编译，尚未完成本版实板测试。
