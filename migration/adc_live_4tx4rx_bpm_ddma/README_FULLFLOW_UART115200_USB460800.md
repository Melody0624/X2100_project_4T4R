# MT-4T4R 全流程实验版：UART2 115200 / USB CDC 460800

烧录目录为 `live_full_experimental`，USBCloner 选择：

`MT-4T4R_live_full_experimental_SFC_NOR_PRESERVE_CONFIG.cfg`

该配置只更新 `0x000000..0x1DAFFF` 程序区，保留
`0x1DB000..0x1FFFFF` 配置/校准区，且 `force_erase=0`。

串口设置：

- SPL 最早期启动日志：3000000、8N1；由现有 SPL 决定。
- FreeRTOS UART2 应用日志：115200、8N1。
- MotorCycle Tools 的 USB CDC COM：460800、8N1。这里的 460800 是 CDC
  line coding，数据实际通过 USB bulk 传输。

本版输出点云 TLV 21、航迹 TLV 22、预警 TLV 24。无需发送
`setRawDataFlg`。USB CDC COM 不能同时由上位机、MobaXterm和采集脚本打开。

BPM 512 项码表与当前源码逐项一致，每帧从 code 0 开始并复位。真实航迹仍受
TX/DDMA 子带映射、硬件通道顺序和16路复数幅相校准未完成的限制，不能作为
道路性能或安全功能验收结果。
