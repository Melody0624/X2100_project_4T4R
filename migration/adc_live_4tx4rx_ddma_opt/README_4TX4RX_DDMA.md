# X2100 + Cheetah4401M 4TX4RX DDMA 性能候选版

该目录是独立版本，不覆盖2TX4RX实时版、ADC回放版、通用4TX4RX框架版或
4TX4RX Walsh-BPM候选版。

## 软件波形假设

- 128个chirp，chirp周期26 us，帧周期50.328 ms；
- 128点Doppler FFT；
- 8个总DDMA子带，每个子带16个Doppler bin；
- TX1-TX4占用四个相邻循环子带，偏移为0、1、2、3；
- 对应逐chirp相位斜坡为0、45、90、135度；
- Cheetah 6-bit相位字的逐chirp增量为0、8、16、24；
- 现有512-chip BPM码作为四路TX的公共0/180度扰码，接收端先解扰；
- TX-major排列的16虚拟通道，暂用`1+0j`单位校准和半波长ULA角度轴。

芯片端候选6-bit相位字为：

```text
phase_word(chirp, tx) =
    (common_bpm_phase + chirp * tx_subband_offset * 8) mod 64
```

其中公共BPM码为负时`common_bpm_phase=32`，否则为0。

## 理论速度性能

- 速度分辨率约0.589 m/s；
- 最大无模糊速度约±37.7 m/s；
- 可以覆盖需求中的±25 m/s测速范围。

这只是波形采样的理论能力，不代表已经满足速度精度、虚警率或跟踪指标。

## 仍待验证

当前`radar_frontend.c`仍使用旧2TX Cheetah寄存器表。必须确认芯片可以按上述
相位字在每个chirp驱动TX1-TX4，并用真实4TX ADC验证子带顺序。还缺少新板
天线坐标、16路幅相校准、CFAR实测标定以及航迹与三雷达融合功能。
