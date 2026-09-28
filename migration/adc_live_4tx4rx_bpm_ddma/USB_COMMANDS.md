# 4T4R USB CDC 命令实验版（2026-09-28）

本版复用发送点云的同一个 USB CDC gadget：只初始化一次 USB，另开接收任务读取以 `\r` 或 `\n` 结束的 ASCII 命令。命令在雷达任务中执行，回复走同一个 CDC IN 端点，因此 PC 端需允许二进制帧之间出现 `OK`/`ERR` 文本。UART2 只输出调试日志；这些命令不从 UART2 接收。当前固件是实验版，4TX/DDMA 映射和16通道幅相校准尚未用已知目标实测确认。

| 命令 | 本版行为 |
| --- | --- |
| `SetFrameCnt -1/0/N` | 当前上电周期持续运行、暂停或再处理 N 帧；暂停时仍取出并释放前端帧以避免缓存堆积。 |
| `SetMaxFrameCnt -1/0/N` | 写入现有 RADV Flash 配置区，读回一致后同时更新本次运行帧数。配置区魔数不匹配时拒绝写入，不自动擦除/重建。 |
| `setRawDataFlg 0/1` | 与原 2T4R 一样，只写入 Flash，读回成功后提示重启；本次运行仍保持原模式。0 为点云/航迹/预警，1 为 type 13 原始 ADC。配置魔数无效时会先只读检查完整 `0x1DB000..0x1FFFFF` 配置分区；仅在全区均为 `FF` 时写入最小配置。只要有非 `FF` 数据就拒绝初始化，不擦除已有配置或校准数据。 |
| `SetDumpFileName NAME` | 在已挂载的 MSC2 FAT 卡 `/mmcblk2p0` 创建 `NAME_adc.dat`；原始 ADC 模式每帧写入同 USB 相同的 ADC 包。仅接受英文字母、数字、下划线和连字符；现有同名文件不覆盖。需有可用 SD 卡及挂载，未在 4T4R 实板验证。 |
| `readreg [0] HEXADDR`、`writereg [0] HEXADDR HEXVALUE` | 访问 Cheetah 0 号芯片；写前必须 `SetFrameCnt 0`，写后需人工确认 RF 配置正确。 |
| `delay MS`、`ResetRams` | 延时最多 1000 ms；ResetRams 必须先暂停处理。 |
| `getBoardVersion`、`getTemperature`、`setAngle DEG` | 查询构建/温度；`setAngle` 只保留标靶角度元数据，不改变整机安装角，也未接入自动标定。 |
| `angCalibMat`、`angFFT`、`uds` | 明确返回 `ERR`。旧 2T4R Flash 只有8个复数通道位，4T4R 需要16通道；旧 UDS/EOL 依赖未经验证的 4TX 角度及尚未绑定的 CAN 传输，不能作为有效校准写入。 |
| `otaUpgrade` | 明确返回 `ERR`；用户提供的当前 NOR 分区为 non-OTA，不能开始升级。 |

`SetMaxFrameCnt` 和 `setRawDataFlg` 使用原配置管理器的 NOR 扇区读—擦—写过程。写入前应备份该板的配置区，写入时保持供电稳定；读回一致只证明写入结果，不是掉电事务保证。

当前验证：主机测试（包括 ASan/UBSan）及 X2100L 交叉编译通过。没有在新固件上做实板 USB 命令、SD 挂载、Flash 参数持久化或 RF 寄存器写入测试；未将合成回归视为真实 4TX 结果。

启动日志中的 `[CMD] flash_read` 和 `magic` 可区分读失败与魔数不匹配。原厂配置魔数的前4字节应为 `56 44 41 52`（小端 `0x52414456`）。检查当前板时可在 UART shell 只读执行 `nor_read 0x1DB000 0x20`；这只能检查开头，不能证明全区为空。不要把另一块板的配置或校准区写进来。
