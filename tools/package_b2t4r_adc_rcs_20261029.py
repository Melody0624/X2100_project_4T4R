"""Verify and export the B ADC/RCS package and its matching Windows source."""
from pathlib import Path
import configparser
import hashlib
import json
import shutil
import struct
import subprocess
import sys

project = Path(__file__).resolve().parents[1]
root = project / 'artifacts/b_2t4r_adc_rcs_20261029'
out = root / 'mips'
base = Path('/home/melody/Manhattan_Project/freertos_b2t4r_adc_fix_20261019')
build = Path('/home/melody/Manhattan_Project/freertos_b2t4r_adc_rcs_20261029')
windows = root / 'windows_source/freertos'
sys.path.insert(0, str(project / 'tools'))
from package_2t4r_tx01_compare import inputs
from verify_vendor2t4r_tx01_cal import symbol_bytes
from verify_vendor2t4r_2M_ota import check_image, PARTITIONS, SPL_NAME

sha = lambda raw: hashlib.sha256(raw).hexdigest()
snapshot = json.loads((root / 'source_snapshot.json').read_text())
assert snapshot == {n: sha(p.read_bytes()) for n, p in inputs(build).items()}
parent_dir = project / 'artifacts/b_2t4r_adc_fix_20261019/mips'
parent = json.loads((parent_dir / 'BUILD_MANIFEST.json').read_text())
assert sha((parent_dir / 'zero_ota.bin').read_bytes()) == parent['rtos_sha256']
reference = json.loads((project / 'docs/2t4r_rcs_reference_20261013.json').read_text())
factory = json.loads((project / 'docs/b2t4r_factory_calibration_20261029.json').read_text())
elf = (out / 'zero.elf').read_bytes()
old_elf = (parent_dir / 'zero.elf').read_bytes()
for name in ('angCalibMat', 'angFFT_interp'):
    assert symbol_bytes(elf, name) == struct.pack('<%df' % len(factory[name]), *factory[name]), name
for name in ('vxPosn_sf1_azm', 'logicIdx_reorder_sf1', 'vxBVIdx_sf1_azm', 'cheetah_default_config'):
    assert symbol_bytes(elf, name) == symbol_bytes(old_elf, name), name
assert symbol_bytes(elf, 'calibration_offset_db') == struct.pack('<f', reference['provisional_offset_db'])
assert symbol_bytes(elf, 'calibration_valid') == b'\x01'
for name in ('rcs_calculate', 'rcs_process_frame'):
    assert symbol_bytes(elf, name), name
rcs_object = (build / 'objs/vendor/motor_cycle_demo/src/rcs_estimation.c.o').read_bytes()
for name in ('rcs_set_reference', 'rcs_clear_calibration', 'rcs_get_frame'):
    assert symbol_bytes(rcs_object, name), name
assert (out / 'build.config').read_bytes() == (parent_dir / 'build.config').read_bytes()
old_usb, usb = bytearray(symbol_bytes(old_elf, 'soc_usb_info')), bytearray(symbol_bytes(elf, 'soc_usb_info'))
for offset in (4, 12):
    old_usb[offset:offset+4] = usb[offset:offset+4] = bytes(4)
assert old_usb == usb and usb[21] == 0

objects = lambda tree: {p.relative_to(tree).as_posix(): sha(p.read_bytes()) for p in tree.rglob('*.o')}
before, after = objects(base), objects(build)
added = sorted(after.keys() - before.keys())
changed = sorted(n for n in before.keys() & after.keys() if before[n] != after[n])
assert not before.keys() - after.keys()
assert added == ['objs/vendor/motor_cycle_demo/src/rcs_estimation.c.o'], added
assert changed == sorted(['objs/vendor/vendor.c.o', 'objs/vendor/motor_cycle_demo/src/board_calib_config.c.o',
    'vendor/builtin.o', 'objs/symbols/func_symbols.c.o', 'symbols/builtin.o',
    'objs/xburst2/lib/show_compile_time.c.o', 'xburst2/builtin.o']), changed
(root / 'OBJECT_DIFF.json').write_text(json.dumps(dict(objects_checked=len(after), added=added, changed=changed), indent=2) + '\n')

# Complete source export, excluding generated build products; never overwrite a different export.
if not windows.exists():
    shutil.copytree(build, windows, ignore=shutil.ignore_patterns(
        'objs', 'symbols', '__pycache__', '*.o', 'zero.bin', 'zero.elf', 'zero.map',
        'zero.dump.S', 'rtos-with-spl.bin', '.config', 'config.h', '.git'))
assert snapshot == {n: sha(p.read_bytes()) for n, p in inputs(windows).items()}, 'Windows source differs'
shutil.copyfile(parent_dir / SPL_NAME, out / SPL_NAME)
assert sha((out / SPL_NAME).read_bytes()) == '351b70b8a68e1c8df04f2976c88f420118b995cd930406541da5b0a8415582fe'
image = check_image(out / 'zero_ota.bin')
cfg_name = 'B_2T4R_ADC_RCS_FACTORY_20261029.cfg'
cfg = configparser.ConfigParser(interpolation=None)
cfg.optionxform = str
assert cfg.read(parent_dir / parent['cfg_file'], encoding='utf-8')
win_out = out.as_posix().replace('/mnt/d/', 'D:/', 1)
for i, name in enumerate((SPL_NAME, 'zero_ota.bin', 'zero_ota.bin')):
    cfg[f'policy{i}']['attribute'] = win_out + '/' + name
cfg['info']['complete'] = cfg['info']['count'] = '0'
assert cfg['sfc']['force_erase'] == '1'
with (out / cfg_name).open('w') as f:
    f.write('; B 2T4R ADC fix + internal RCS reference + factory angle defaults; 2026-10-29\n')
    cfg.write(f, space_around_delimiters=False)
for i, value in enumerate(PARTITIONS):
    assert cfg['spiPartition'][f'Partition{i}'].strip('"') == value

text = f'''# B 2T4R ADC、RCS 与厂家角度默认值（2026-10-29）

烧录配置：{cfg_name}。镜像 {len(image)} 字节，单槽剩余 {0xEA000-len(image)} 字节。
对应完整 Windows 源码：D:/downloads/X2100_project-main/artifacts/b_2t4r_adc_rcs_20261029/windows_source/freertos。
WSL 构建目录：{build}。

严格以 B_2T4R_ADC_FIX_20261019 为基线，只接入 RCS 模块/帧调用及厂家两张默认表，共五个源码文件变化。
USB 驱动、USB 发送器、波形、检测/CFAR/测角/后处理、ADC 修正及阵元几何保持基线。
angCalibMat 使用厂家新版原始 BOARD_TYPE_BARRACADE 八通道复数系数；angFFT_interp 使用该板型原始 128 项角度表。
两张表全部 float32 值已核对链接镜像。它们是编译默认值，主动校准及有效保存配置仍可覆盖。
force_erase=1：烧录清空保存配置，启动使用厂家默认表。初始输出点云/航迹，setRawDataFlg 1 后重新上电切换 ADC。

RCS 标定接口与默认偏置 −136.526320837 dB 已启用；来自用户 4.6 dBsm 角反、113 帧、雷达估计距离 1.18993322 m。
属于单距离初步标定。此系数来自之前用户幅相补偿版本，切换厂家幅相后应重新验证/标定，不能视为新版本绝对 RCS 精度已验证。
rcs_set_reference、rcs_clear_calibration、rcs_get_frame 接口已在源码/目标文件中验证；当前没有外部命令调用，链接器会移除未调用函数。
后续接入标定命令或 CAN 时可直接调用这些接口；重启恢复编译默认。未增加 USB/TLV/CAN 输出字段。

ADC 100 帧完整数据、重复初始化/分配失败/DSP 保持及 RCS 标定/无效输入/高度/容量 ASan+UBSan 回归通过。
完整 clean MIPS 编译通过。USB 与信号处理对象不变；新增 RCS 对象，其他差异仅主流程、校准表、符号表和编译时间。
原始 SPL、OTA 双槽、配置区边界及文件/源代码哈希通过。此新组合尚未上板测试。
原 B 包、四发源码及 Cloner spl/img 均未覆盖；Cloner configs/x2100 只新增同名配置。
Windows 主 B 源码也已更新厂家两张默认表，且保留 RCS 与 ADC 修正；该主目录含其他后续维护改动，不能等同本包的完整冻结源码。
'''
(out / 'README_烧录说明.md').write_text(text, encoding='utf-8')
shutil.copyfile(project / 'docs/2t4r_rcs_reference_20261013.json', out / 'RCS_REFERENCE.json')
shutil.copyfile(project / 'docs/b2t4r_factory_calibration_20261029.json', out / 'FACTORY_CALIBRATION.json')
m = dict(parent)
m.update(date='2026-10-29', name='B 2T4R ADC fix, internal RCS reference and factory calibration',
    cfg_file=cfg_name, source_sdk=str(windows), build_sdk=str(build), source_sha256=snapshot,
    parent_package=str(parent_dir), parent_rtos_sha256=parent['rtos_sha256'],
    rtos_bytes=len(image), rtos_sha256=sha(image),
    angle_mapping='factory_defaults_with_saved_calibration',
    linked_calibration_hex=symbol_bytes(elf, 'angCalibMat').hex(), hardware_tested=False,
    factory_calibration_source=factory['source'], factory_calibration_sha256=factory['source_sha256'],
    rcs=dict(internal_only=True, protocol_fields_added=False, default_calibrated=True,
             provisional=True, offset_db=reference['provisional_offset_db'], reference_dbsm=4.6,
             reference_range_estimate_m=reference['reference_range_estimate_m'],
             source_reference_sha256=reference['sha256'], verified_with_factory_coefficients=False,
             unused_api_linker_gc=True), files={})
for name in ('zero_ota.bin', 'zero.elf', SPL_NAME, 'build.config', cfg_name,
             'README_烧录说明.md', 'RCS_REFERENCE.json', 'FACTORY_CALIBRATION.json'):
    raw = (out / name).read_bytes()
    m['files'][name] = dict(bytes=len(raw), sha256=sha(raw))
(out / 'BUILD_MANIFEST.json').write_text(json.dumps(m, ensure_ascii=False, indent=2) + '\n')
(out / 'SHA256.txt').write_text(''.join(f"{v['sha256']}  {k}\n" for k, v in m['files'].items()))
subprocess.run([sys.executable, str(project / 'tools/verify_vendor2t4r_2M_ota.py'), str(out)], check=True)
cloner = project / 'MotorCycle_Ingenic/MotorCycle_Ingenic/tools/USBCloner/cloner-latest-windows/cloner-latest-windows/cloner-2.5.58.5-windows_alpha/cloner-2.5.58.5-windows_alpha/configs/x2100'
target = cloner / cfg_name
assert not target.exists() or target.read_bytes() == (out / cfg_name).read_bytes()
shutil.copyfile(out / cfg_name, target)
identity = dict(configuration=cfg_name, image_sha256=sha(image), image_bytes=len(image),
    source_export=str(windows).replace('/mnt/d/', 'D:/', 1),
    build_sdk=str(build), parent_image_sha256=parent['rtos_sha256'],
    factory_source_sha256=factory['source_sha256'], rcs_offset_db=reference['provisional_offset_db'],
    source_inputs_checked=len(snapshot), source_changes=json.loads((root / 'SOURCE_DIFF.json').read_text())['changed_files'],
    source_change_sha256={n:snapshot[n] for n in json.loads((root / 'SOURCE_DIFF.json').read_text())['changed_files']})
(project / 'docs/b2t4r_adc_rcs_factory_20261029.json').write_text(json.dumps(identity, ensure_ascii=False, indent=2) + '\n')
print('PASS: factory 8 complex/128 angle values, RCS boot default, unchanged USB/DSP objects, exact Windows source, OTA package')
print('IMAGE_SHA256', sha(image))
print('CONFIG', target)
