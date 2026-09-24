# 四发四收 MATLAB：第 1–6 步

本目录独立于旧两发 MATLAB 工程。当前实现从合成 ADC 或指定格式文件到距离 FFT、BPM、Doppler FFT、DDMA、CFAR、测角、坐标变换和点云导出的完整离线链。尚无四发实测数据验证，不包含跟踪、实时串口或俯仰角估计。

在 MATLAB 中运行：

```matlab
cd('D:\downloads\X2100_project-main\matlab_4tx4rx');
result = demo_stage1;
result = demo_stage2; % 第二步：含第一步数据生成及距离处理
result = demo_stage3; % 第三步：含前两步及公共 BPM / Doppler 处理
result = demo_stage4; % 第四步：DDMA 判定、速度和 16 路虚拟通道
result = demo_stage5; % 第五步：CFAR 与方位角估计
run = demo_stage6;    % 第六步：双目标点云、统一入口
files = radar.exportRun(run,fullfile(pwd,'outputs','stage6'));
results = runtests('tests');
assert(all([results.Passed]));
```

`result.adc` 是去掉 offset 的量化实数 ADC，维度为 `[506,128,4]`。
`result.rawWords` 是左移 4 位的 12 位 offset-binary 存储字，尚不含帧头、chirp 头或字节交织，不能直接当作设备 `.dat` 文件。
修改 `demo_stage1.m` 的 targets 结构体数组可添加多个目标。幅度单位为每个 TX 回波的 ADC counts；噪声标准差在配置中设置。每次使用局部固定随机种子，不改变 MATLAB 全局随机状态。

模型使用固定距离、固定角度的远场理想阵列，正速度对应正慢时间频率；不模拟距离迁移、距离速度耦合、多径或硬件失配。公共 BPM 使用现有工程码表前 128 项，各 TX 的 DDMA 相位增量为 0/45/90/135 度每 chirp。该配置用于软件仿真，不代表已经确认真实硬件编码。

虚拟通道按 TX 优先排列，位置为 TX 位置与 RX 位置之和，形成理想半波长 16 阵元 ULA。校准字段预留给后续测角，当前生成器不施加硬件误差。无实测数据时只能验证模型下的软件行为。

第二步接口：`range = radar.processRange(adc,cfg)`。输入必须为解包后去掉 offset 的浮点 ADC counts，不可传入原始 uint16 存储字。沿每个 chirp/RX 的快时间独立减均值，应用 506 点对称 Blackman 窗，补零执行 512 点 FFT。采用双精度数学实现，尚未验证与固件的数值逐点一致性。

`result.range.cube` 为 `[256,128,4]` 复数频谱，保留零基索引 0–255，排除 Nyquist 点；保留相位供后续处理。距离格间隔约 0.399703 m，采样带宽对应的理论距离分辨率约 0.404443 m（加窗还会展宽主瓣）。FFT 不做幅度归一化，显示功率为相对 dB，不是 dBm。图中的 range/chirp 热图尚不是距离–多普勒图。`peakRangeM` 仅为最强距离格，无检测门限；噪声也会有峰值，不能视为 CFAR 检测。严格零输入时 `hasSignal=false`、峰值为 NaN。

第三步接口：`doppler = radar.processDoppler(range.cube,cfg)`。沿 chirp 维乘公共 BPM 的 ±1 码进行解扰，再使用固件公式 `w[n]=0.5*(1-cos(2*pi*(n+1)/(L+1)))` 加窗并执行 128 点 FFT；未减慢时间均值，以保留静止目标。窗口公式与固件一致，但使用双精度，未做固件逐点对照。

`result.doppler.cube` 为 `[256,128,4]` 复数频谱，Doppler 维保持未 fftshift 的 0–127 零基索引，供下一步 DDMA 解码。`shiftedCube`、`shiftedPower` 和 `apparentVelocityAxisMps` 用于显示，速度格约 0.588770 m/s。四路 TX 各相差 16 个频率格；图中的四个峰是单目标的四路 TX 调制频谱，并不表示四个目标。横轴为含 DDMA 频偏的表观速度，不能直接作为目标速度或已验证无模糊范围。演示的 TX 真值虚线来自仿真参数，处理函数不使用目标真值。第三步仍只有四路 RX，尚未生成 16 路虚拟通道。

第四步接口：`ddma = radar.decodeDdma(doppler.cube,cfg)`，必须传入未 fftshift 的复数频谱。当前仅支持 8 子带、连续四路 TX 偏移 0:3。每个距离/折叠 Doppler 单元提取八个子带的 RX 幅度和，枚举循环四发模板，取四路最小幅度评分。候选接近、弱 TX、空子带污染的规则和默认比例来自 `migration/adc_live_4tx4rx_bpm_ddma/ddma_resolver.c` 和 `radar_config.h`；使用双精度，未做 C 程序逐点数值对照。阈值为工程初值，尚未用实测数据标定。

输出 `status` / `valid` / `velocityMps` 为 `[256,16]`。状态依次为 0 空、1 通过、2 候选歧义、3 空带污染、4 弱 TX、5 无效。`candidates` 为八个候选的位图，`anchor`、`signedBin`、`txDopplerBins` 字段均使用零基索引；MATLAB 数组下标仍从 1 开始。速度按恢复出的有符号 FFT 格点计算，不作亚格插值。`quality` 只是相对诊断分数，不是概率。

`virtualCube` 为 `[256,16,16]`，依次为距离、折叠 Doppler、虚拟通道；通道顺序 TX1/RX1..4、TX2/RX1..4 等，保留复数相位，暂不应用校准。判定拒绝时速度及虚拟通道为 NaN；`amplitude` 仍保留候选四带幅度和，供下一步 CFAR 背景估计，不能把拒绝单元清零。`amplitudeDb` 为未归一化幅度的 20log10，不是 dBm。

通过 DDMA 只说明符合当前单目标模板，不表示已检测到目标；噪声、旁瓣也可能通过。演示仅显示最强通过单元，尚无 CFAR 门限。同距离、同折叠 Doppler 的重叠目标可能被拒绝，也可能形成无法由该模板识别的错误假设；此实现不保证任意多目标解模糊。128 个速度格的测试验证索引映射，不证明真实硬件的无模糊测速范围。演示中的真值仅用于绘图对照，解码函数不接收目标真值。

第五步接口：`detection = radar.detectAndEstimate(ddma,range.rangeAxisM,cfg)`。
CFAR 使用未清零的 `ddma.amplitudeDb`，在 dB 域平均训练单元，沿用固件 Doppler CASO（6 dB）和 Range CAGO（3 dB）公式、循环训练窗及局部峰值规则。每侧训练 4 格、保护 2 格；距离首尾格不输出检测。门限是工程初值，不是指定虚警概率的功率域 CFAR。两个维度的门限均计算并保留，最终取两者与 DDMA valid 的交集。`cfarMarginDb` 是超过两维门限的最小余量，不是 SNR。

输出 `detections` 结构体包含距离、速度、方位角、未归一化幅度 dB、CFAR 余量和 DDMA 分数；`rangeRow`、`foldedColumn` 为 MATLAB 一基下标。按幅度降序限制为 `cfg.cfar.maxDetections`，超出数量记录在 `overflowCount`。`acceptedMask` 是截断前交集，`outputMask` 对应实际输出。空场景返回空结构体数组；无人工补点。噪声可能形成虚警，固定种子测试不能证明实测虚警率。

测角以 TX 优先顺序将 16 路数据乘 `cfg.calibration` 复数校正系数（若实测通道误差为 g，校正量应为 1/g）。应用 16 点固件公式 Hanning 窗。升序半波长 ULA 使用 128 点角度 FFT，在空间频率上作三点抛物线插值；其他一维位置使用阵列导向矢量扫描，默认 -90:0.1:90 度。不估计俯仰角，也不保证消除非均匀阵列栅瓣或端射角歧义。角度符号沿用生成器的正空间相位约定；尚未进行安装旋转。

`angles` 保存每个输出检测的角度谱、坐标轴、方法及校正后的通道数据。处理函数不读取目标真值；演示中真值仅用于对照。当前浮点实现未与 C 固件逐点对照，仿真角度误差不是实测精度。

第六步统一入口：`run_4tx4rx(targets,cfg)` 生成单帧仿真；`run_4tx4rx(filePath,cfg,frameIndices)` 回放所选文件帧，默认仅读第 1 帧，支持例如 `1:20`。文件模式必须显式传入配置。内部共享 `radar.processFrame(adc,cfg)`；该函数保留中间数据，而统一入口只保留每帧检测、点云和耗时以降低多帧内存用量。合成 `targets` 结构体传空表示空场景，不会保留旧点云。

```matlab
cfg = config_4tx4rx();
% 按真实采集资料修改 BPM、chirp 周期、阵列位置和校准等参数。
% 不要把旧两发文件作为四发验证数据；文件头不能识别 TX 编码。
frame = radar.readAdcFrame('D:\data\four_tx_adc.dat',1,cfg);
run = run_4tx4rx('D:\data\four_tx_adc.dat',cfg,1:10);
files = radar.exportRun(run,fullfile(pwd,'outputs','replay'));
```

读取器只支持当前工程已有的固定 ADC-only 协议：28 字节帧头、一个 8 字节 TLV（type=13、长度不含 TLV 头）、每 chirp 32 字节头和 506×4×2 字节 ADC。默认每包 522276 字节。ADC 为 RX 交织、小端、12 位 offset-binary 左移 4 位。校验文件整包长度、magic、包长、TLV、低四位及饱和比例（默认上限 1%）；不自动重同步，不支持混合 TLV、纯 payload 或其他设备格式。chirp 头仅原样保留，未知字段不作语义校验。`frameNumberGapAfter` 记录所选帧序列中非连续编号的位置（支持 uint32 回卷）；若人为跳帧选取也会记录间隔，不能直接当作采集丢帧。读取器只验证实际读取的帧头，未选帧不逐个扫描。

读取成功只证明格式满足这些检查，不证明四发波形匹配，`waveformVerified=false` 保留该事实。测试使用生成的协议夹具验证读取，尚未在真实四发 ADC 文件上运行。`rawWords` 不是带帧头文件，不能直接 fwrite 后交给读取器。

点云采用雷达 x 向前、y 指向正方位角：`x=r*cos(theta)`、`y=r*sin(theta)`。`cfg.pointCloud.installAngleDeg` 是逆时针安装旋转，默认 0°，随后加 `translationM=[x y]`。距离和视场过滤在雷达坐标下执行，默认最小 0.5 m、最大无限、方位角 ±60°，可配置。没有擅自套用旧固件 180° 安装角。速度字段始终是有符号径向速度，不推算二维目标速度；没有地面/倍频经验过滤。

`radar.exportRun(run,outputStem)` 显式保存 MAT 和 JSON，并覆盖同名输出；演示本身只绘图不自动保存。MAT 保存完整配置及紧凑结果；JSON 保存数据来源、坐标约定、安装参数及逐帧检测/点云。空帧仍导出，防止消费端保留上一帧点。耗时为本机 MATLAB 处理耗时，不是开发板实时性指标。
