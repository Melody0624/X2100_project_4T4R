# 角反 RCS 初步标定：2026-10-13

输入：`Record_20261013_111210.dat`；用户提供角反标称 RCS **4.6 dBsm**，位置约正前方 1 m。

## 解析与目标选择

- 文件完整解析为 113 帧，帧号 2027～2139，总计 310 个检测点。
- 点云为 `legacy-float60` 格式，`power` 已是浮点 dB 值，无需再次除以 100。
- 选择范围：距离 0.5～2 m、绝对方位角 ≤15°、绝对速度 ≤0.5 m/s、峰值标记为 1。
- 每帧恰有一个点满足条件，共 113 个；与用户描述的近距离角反位置相符。文件无法独立确认目标物理身份。
- 点云 `range_m` 与 `sqrt(x²+y²)` 一致，最大差异小于 0.0000001 m。未采用量化后的航迹坐标作为参考距离。

| 量 | 结果 |
|---|---:|
| 点云平均距离 | 1.189933 m |
| 距离中位数 | 1.188770 m |
| 距离最小～最大值 | 1.177500～1.214585 m |
| 距离标准差 | 0.008328 m |
| 平均回波强度 pwr | 138.105841 dB |
| 平均 SNR | 45.511150 dB |
| 平均方位角 | +3.951278° |
| 平均输出坐标 (x,y) | (1.186923, 0.081879) m |
| 径向速度 | 全部为 0 m/s |

以上距离是雷达测量结果，帧间标准差不代表绝对测距精度。文件未记录安装高度模式，若输出距离经过地面投影，还需按实际安装高度恢复斜距。

## 初步偏置

按照当前模块定义，对选中点逐帧计算并取平均：

```text
mean_relative_db = mean(pwr + 40*log10(range_m)) = 141.126320837
offset_db        = 4.6 - mean_relative_db         = -136.526320837
RCS_dBsm         = pwr + 40*log10(slant_range_m) - 136.526320837
```

对应标定接口参数（假设文件距离就是雷达斜距）：

```c
rcs_set_reference(4.6f, 1.18993322f, 138.10541727f);
```

第三个参数为保持逐帧平均距离补偿一致而换算的有效强度，与简单强度均值仅差 0.000424 dB。
该偏置使参考数据均值为 4.6 dBsm，这是标定定义的结果，不能用来证明测量准确。

## 使用范围

记录分析后，已按用户要求将该偏置加入 B 版 2T4R 启动默认值。运行中重新标定或清除的结果保存在 RAM，重启恢复编译默认值；没有增加 Flash 参数或输出协议字段。

该值是单距离初步标定，只适用于本次记录对应的板子、波形、收发通道及增益设置。记录无法确认 ADC 是否饱和，也无法排除多径或近场影响。建议补充独立测量的实际斜距，以及另外两个距离的角反记录，验证距离补偿后结果是否一致。不能直接移用于新的 4T4R 波形。

## 本地分析文件

- `analyze_reference.py`：统计和偏置计算，可直接运行。
- `reference_analysis.json`：完整数值、输入 SHA256 与假设。
- `corner_selected_points.csv`：逐帧参考点、相对 RCS 及本次偏置换算结果。
- `Record_20261013_111210_detections.csv`：全部检测点。

这些本地分析文件位于 `artifacts/rcs_reference_20261013_111210`，不随 Git 提交。仓库中的 `docs/2t4r_rcs_reference_20261013.json` 保留参考统计与记录 SHA256，烧录包使用该文件生成标定说明。

## B 版默认值与重新构建

源码 `vendor/motor_cycle_demo/src/rcs_estimation.c` 在两发四收配置下默认启用本次偏置；运行中重新标定或清除仍有效，重启恢复编译默认值。输出协议没有新增 RCS 字段。

在现有 WSL 构建环境执行：

```bash
project=/mnt/d/downloads/X2100_project-main
BUILD_DIR=/home/melody/Manhattan_Project/freertos_rcs_reference_20261013 \
BUILD_OUTPUT="$project/artifacts/2t4r_rcs_reference_20261013" \
bash "$project/tools/build_2t4r_rcs_internal.sh"
python3 "$project/tools/package_2t4r_rcs_reference.py" \
  /home/melody/Manhattan_Project/freertos_rcs_reference_20261013 \
  "$project/artifacts/2t4r_rcs_reference_20261013"
python3 "$project/tools/verify_vendor2t4r_2M_ota.py" \
  "$project/artifacts/2t4r_rcs_reference_20261013"
```

沿用现有本地 SDK 构建配置、厂家工具链及 OTA SPL；生成配置为 `B_2T4R_RCS_REF4p6_8ch_20261013.cfg`，`force_erase=1`。无需原始 DAT 即可重建固件；从零复核标定统计仍需原始记录。
