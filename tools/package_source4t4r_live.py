"""Create a separate 2 MiB OTA flash package for experimental 4TX live tests."""
from pathlib import Path
import configparser
import hashlib
import json
import shutil
import struct
import sys

from verify_vendor2t4r_tx01_cal import symbol_bytes
from verify_vendor2t4r_2M_ota import check_image, PARTITIONS, SPL_NAME

out, baseline = map(Path, sys.argv[1:])
manifest=json.loads((out/'SOFTWARE_BUILD_MANIFEST.json').read_text())
assert manifest['rf_ready'] and manifest['experimental_live']
assert manifest['tx']==4 and manifest['rx']==4 and manifest['virtual_channels']==16
assert manifest['fstep']==171 and manifest['rf_rows']==93
source=Path(manifest['source_sdk'])
snapshot=json.loads((out/'source_snapshot.json').read_text())
for name,digest in snapshot.items():
    assert hashlib.sha256((source/name).read_bytes()).hexdigest()==digest,name
image=check_image(out/'zero_ota.bin')
inputs=json.loads((out/'BASELINE_BUILD_INPUTS.json').read_text())
spl_source=Path(inputs['spl_source'])
assert hashlib.sha256(spl_source.read_bytes()).hexdigest()==inputs['spl_sha256']
shutil.copyfile(spl_source,out/SPL_NAME)
elf=(out/'zero.elf').read_bytes()
matrix=symbol_bytes(elf,'angCalibMat')
assert len(matrix)==128 and matrix in image
header=(source/'vendor/motor_cycle_demo/inc/radar_types.h').read_text()
assert '#define SOURCE_4T4R_RF_READY 1' in header

cfg_name='MT4T4R_Source_16p699_16CH_256P32T_OTA_20261014.cfg'
label='4T4R 源码算法实验上板版 · 16.699 MHz/us · 16通道 · 256点/32航迹'
cfg=configparser.ConfigParser(interpolation=None);cfg.optionxform=str
template=baseline/'MotorCycle_Ingenic/MotorCycle_Ingenic/tools/USBCloner/common/configs/x2100/x2100x_cheetah_sfc_nor_2M_ota_freertos.cfg'
assert cfg.read(template),template
win_out=out.as_posix().replace('/mnt/d/','D:/',1)
for i,name in enumerate((SPL_NAME,'zero_ota.bin','zero_ota.bin')):
    cfg[f'policy{i}'].update(enabled='1',attribute=win_out+'/'+name,
                            label=('uboot','rtos_0','rtos_1')[i],
                            offset=('0x000000','0x6000','0xF0000')[i])
for i in (3,4):cfg[f'policy{i}'].update(enabled='0',attribute='')
cfg['spiPartition']['count']='5'
for i,part in enumerate(PARTITIONS):cfg['spiPartition'][f'Partition{i}']='"'+part+'"'
cfg['sfc']['force_erase']='1'
with (out/cfg_name).open('w',encoding='utf-8') as f:
    f.write('; '+label+'\n');cfg.write(f,space_around_delimiters=False)
manifest.update(date='2026-10-14',name=label,cfg_file=cfg_name,flashable_package=True,
                source_sha256=snapshot,linked_calibration_hex=matrix.hex(),
                external_spl_source=str(spl_source),point_limit=256,track_limit=32,
                angle_mapping='theory_defaults_with_saved_calibration',
                rcs=dict(internal_only=True,absolute_calibrated=False),
                phase_calibration=dict(tx_mask=15,bands=8,unique_phases=[1,8,4,8],
                    scan_sample_rate_hz=53330000,scan_slope_hz_per_s=19531250000000,
                    check_slope_hz_per_s=16699218750000,hardware_verified=False),files={})
for name in ('zero_ota.bin',SPL_NAME,'zero.elf','build.config',cfg_name):
    raw=(out/name).read_bytes()
    manifest['files'][name]=dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
(out/'BUILD_MANIFEST.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(out/'SHA256.txt').write_text(''.join(f"{v['sha256']}  {k}\n" for k,v in manifest['files'].items()))
values=struct.unpack('<32f',matrix)
matrix_rows='\n'.join(f'| TX{i//4}RX{i%4} | {values[2*i]:.6f} | {values[2*i+1]:.6f} |' for i in range(16))
(out/'README_烧录说明.md').write_text(f'''# {label}

## 烧录配置

配置：`{cfg_name}`。镜像路径已指向本目录，请保持目录位置。
RTOS：`zero_ota.bin`，{len(image)} 字节，单槽剩余 {0xEA000-len(image)} 字节。
SPL：`{SPL_NAME}`，沿用此前已核验的 24576 字节 OTA SPL，不是新编译的 SPL。
两份 RTOS 槽均写入同一个新镜像。不要将含 SPL 的 rtos-with-spl.bin 写入 RTOS 槽。

| 分区 | 起点 | 长度 |
|---|---|---|
| uboot | 0x000000 | 0x6000 |
| rtos_0 | 0x006000 | 0xEA000 |
| rtos_1 | 0x0F0000 | 0xEA000 |
| boot_flag | 0x1DA000 | 0x1000 |
| config | 0x1DB000 | 0x25000 |

force_erase=1，烧录会清除旧配置和旧标定，首次启动使用本包编译默认值。
原 B 2T4R 工程和原 Cloner spl/img 文件保留。此包适用于沿用当前 X2100L、2 MiB NOR、MCP2515 的测试板配置。

## 已启用的功能

SOURCE_4T4R_RF_READY=1，启动不再因诊断开关退出；进入正常射频配置和实时 ADC 接收流程。
4TX4RX、16 虚拟通道；26.665 MS/s、506 点、128 chirp、26 μs、50.328 ms。
FSTEP=0xAB，实际斜率 16.69921875 MHz/μs；软件距离换算仍用原先约定的 16.699 MHz/μs。
保留源码检测、BPM/DDMA、CFAR、测角和后处理，厂家跟踪/自车速度接口保持。
点云上限 256、航迹上限 32，现有 USB 协议不变；RCS 仅内部计算，尚无四发绝对标定。
默认角度表为 76.5 GHz、−1.96 mm、128 点 FFT 的理论映射。

## 默认幅相补偿

| 通道 | 实部 | 虚部 |
|---|---:|---:|
{matrix_rows}

来自 Record_20260928_202805_adc.dat。angCalibMat write 需 32 个 float，read 返回 16 行；有效 Flash 校准可覆盖默认值。

## 校准参数适配

阵列零相和角度校准使用 16 通道。硬件移相器校准使用全部四路 TX、8 子带及相位增量 0/45/90/135°。
粗扫/细扫沿用专用寄存器表：53.33 MS/s、19.53125 MHz/μs；收敛检查复制新正常工作表，并同步新斜率。
校准完成后的停止/重启行为沿用原实现，未增加自动重启策略。

## 上板验证

这是实验测试包，编译和离线验证不能证明探测性能或硬件校准已经通过。
启动 UART 应出现 [4T4R] EXPERIMENTAL LIVE 和后续 camera/USB 初始化日志。
先检查连续采样和正常点云，再用已知距离、左右位置与运动方向确认距离、速度、角度。
尚需复核新波形下幅相补偿、移相器校准及绝对 RCS。
''',encoding='utf-8')
print('PASS: separate experimental 4T4R OTA cfg, SPL, dual RTOS slots and manifest created')
