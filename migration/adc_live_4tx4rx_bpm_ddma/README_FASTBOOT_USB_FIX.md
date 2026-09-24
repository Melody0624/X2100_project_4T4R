# MT-4T4R 快速启动及USB连续输出修复版

烧录 `live_full_experimental` 目录内的：

`MT-4T4R_live_full_experimental_SFC_NOR_PRESERVE_CONFIG.cfg`

本版相对上一版的变化：

- 关闭产品板未使用的MSC2/MMC初始化，不再打印
  `msc2: mmc not find devices`。
- 关闭未安装PHY的MAC/LwIP初始化，去掉约10秒的以太网超时等待。
- UART2应用日志为115200；USB CDC line coding为460800。
- 点云/航迹USB写等待从5 ms增加到1000 ms，并保留3次重试。
- 算法和跟踪仍按每帧约19.87 Hz运行；上位机包每2帧发送一次，约9.94 Hz，
  降低MotorCycle Tools绘图和逐帧日志压力。
- `[HOST]`日志新增`last_error`和`consecutive`，用于区分短暂超时和连续断流。

该CFG只更新`0x000000..0x1DAFFF`程序区，保留
`0x1DB000..0x1FFFFF`配置/校准区，`force_erase=0`。

如果上位机再次停止，请记录停止前后的完整`[HOST]`行。`sent`继续增长说明PC
解析/绘图停止；`errors`和`consecutive`增长说明USB发送阻塞，`last_error`给出
驱动错误码。真实4T4R算法的TX/DDMA映射和幅相校准边界不因本修复而改变。
