# X2100 前端 ADC 采集与 MATLAB 处理

本目录与当前固件参数配套：506 个采样点、128 个 chirp、4 路 RX、2 路 TX、BPM/DDMA。

## 1. 固件采集模式

当前源码已经设置：

- `SAVE_RAW_DATA = 1`
- `CHEETAH_SAVE = 1`
- `RAW_ADC_CAPTURE_FRAMES = 100`

插入 FAT32 TF 卡后上电，固件会把数据保存为：

```text
/mmcblk2p0/adc_YYYYMMDD_HHMM_0.bin
```

100 帧约为 52,230,000 字节（约 49.8 MiB）。原始采集模式帧周期约 249.84 ms，理论采集时间约 25 秒。

串口应看到：

```text
[ADC_CAPTURE] raw capture enabled, frame limit=100
Total ADC size per frame: 522240 bytes
```

等待出现 `cheetah_stop completed` 后再断电或拔 TF 卡，避免最后一帧和文件系统缓存尚未写完。

## 2. MATLAB 使用

将 TF 卡中的 `.bin` 文件复制到电脑，打开 `demo_x2100_adc.m`，修改：

```matlab
dataFile = "D:\data\adc_20260814_1530_0.bin";
frameIndex = 1;
```

然后运行脚本。读取后的 ADC 数组维度为：

```matlab
size(frame.adc)   % 506 x 128 x 4，维度依次为 sample/chirp/RX
```

## 3. 当前复现范围

`x2100_process_frame.m` 已按固件实现以下阶段：

1. 12 位 ADC 解包：`(uint16 >> 4) - 2048`
2. 每个 chirp、每个 RX 独立去直流
3. Blackman 距离窗和 512 点距离 FFT
4. 固件 Hanning 多普勒窗
5. 128 点 BPM 固定码解扩和 Doppler FFT
6. 4 RX 非相干幅度累积
7. 4 子带 DDMA 选择和 RD 图生成

尚未移植的是 2D CFAR、距离插值、8 虚拟阵元幅相校准、方位 FFT、点云后处理和跟踪。这些阶段需要先用实际 ADC 文件验证帧头、量程、目标方向和标定矩阵后再继续，以免把硬件连接或数据格式问题带入算法。

## 4. 文件格式

每帧总长 522,300 字节：

| 部分 | 字节数 |
|---|---:|
| `Mmw_output_message_header` | 28 |
| TLV header | 8 |
| 附加角度字段（6 x int32） | 24 |
| 原始 ADC（128 个 chirp） | 522,240 |

每帧的 522,240 字节 ADC 区域包含 128 个 chirp。每个 chirp 先是 32 字节前端头，随后是 506 组采样；每组按 RX0、RX1、RX2、RX3 顺序交错保存。
