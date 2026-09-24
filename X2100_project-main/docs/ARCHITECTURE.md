# 系统架构

## 数据链路

1. Cheetah4401M完成毫米波收发、4路RX采样和MIPI CSI-2输出。
2. X2100 CSI/VIC通过DMA将RAW12帧写入DDR。
3. CPU0解析Chirp头并将4路RX ADC解交织。
4. CPU1执行去直流、距离FFT和多普勒FFT。
5. CPU0执行DDMA解码、RD图、CFAR、测角和后处理。
6. 跟踪/预警库生成航迹，再通过USB、UART、CAN或TCP输出。

## 当前2T4R参数

- TX：2
- RX：4
- 虚拟通道：8
- Chirp：128
- ADC长度：506
- MIPI：4 Lane

## 关键入口

- 启动：`firmware/x2100/freertos/xburst2/init.c`
- 雷达应用：`firmware/x2100/freertos/vendor/vendor.c`
- ADC解析：`firmware/x2100/freertos/vendor/motor_cycle_demo/src/get_adc_from_dat_file.c`
- 检测：`firmware/x2100/freertos/vendor/motor_cycle_demo/src/detection/detection_processing.c`
- 测角：`firmware/x2100/freertos/vendor/motor_cycle_demo/src/detection/angle_estimation_v1.c`

跟踪和预警实现以静态库形式提供，检测、CFAR和测角源码可修改。
