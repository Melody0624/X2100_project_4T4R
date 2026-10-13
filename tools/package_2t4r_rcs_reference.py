"""Package the B 2T4R build with the supplied provisional corner reference."""
from pathlib import Path
import hashlib
import json
import math
import shutil
import struct
import sys

from package_2t4r_tx01_compare import PROJECT, SPL, finish
from verify_vendor2t4r_tx01_cal import symbol_bytes

source = PROJECT / 'X2100_project-main/firmware/x2100/freertos'
build, output = map(Path, sys.argv[1:])
compiler = PROJECT / 'MotorCycle_Ingenic/MotorCycle_Ingenic/tools/toolchains/mips-xburst2-newlib430/bin'
reference_dir = PROJECT / 'docs'
reference_path = reference_dir / '2t4r_rcs_reference_20261013.json'
reference = json.loads(reference_path.read_text(encoding='utf-8-sig'))
elf = (output / 'zero.elf').read_bytes()
assert symbol_bytes(elf, 'calibration_offset_db') == struct.pack('<f', reference['provisional_offset_db'])
assert symbol_bytes(elf, 'calibration_valid') == b'\x01'
assert symbol_bytes(elf, 'rcs_calculate') and symbol_bytes(elf, 'rcs_process_frame')
spacing = -0.00196 * 76.5e9 / 299792458.0
for i, value in enumerate(struct.unpack('<128f', symbol_bytes(elf, 'angFFT_interp'))):
    expected = math.degrees(math.asin((i if i < 64 else i - 128) / (128 * spacing)))
    assert abs(value - expected) < 1e-5
shutil.copyfile(build / 'zero.bin', output / 'zero_ota.bin')
shutil.copyfile(build / 'xburst2/soc-x2000/spl' / SPL, output / SPL)
assert (build / 'zero.elf').read_bytes() == elf
finish(source, build, output, compiler, 'B_updated_source_arraycal')

old_name = 'B_2T4R_arraycal_8ch.cfg'
new_name = 'B_2T4R_RCS_REF4p6_8ch_20261013.cfg'
label = 'B：源码算法 2T4R · 4.6 dBsm 初步 RCS 标定 · 用户八通道'
cfg_text = (output / old_name).read_text(encoding='utf-8')
(output / new_name).write_text('; ' + label + '\n' + cfg_text.split('\n', 1)[1], encoding='utf-8')
(output / old_name).unlink()  # Remove only the intermediate cfg created by finish above.
shutil.copyfile(reference_path, output / 'RCS_REFERENCE.json')
shutil.copyfile(reference_dir / '2t4r_rcs_reference_20261013.md', output / 'RCS初步标定结果.md')
shutil.copyfile(PROJECT / 'docs/2t4r_rcs_internal_20261012.md', output / 'RCS标定说明.md')
manifest_path = output / 'BUILD_MANIFEST.json'
m = json.loads(manifest_path.read_text(encoding='utf-8'))
m.update(name=label, date='2026-10-13', cfg_file=new_name)
m['rcs'] = dict(default_calibrated=True, provisional=True, protocol_fields_added=False,
                offset_db=reference['provisional_offset_db'], reference_dbsm=4.6,
                estimated_reference_range_m=reference['reference_range_estimate_m'],
                reference_record_sha256=reference['sha256'], reference_points=113,
                runtime_storage='RAM; reboot restores compiled default')
m['files'].pop(old_name)
for name in (new_name, 'RCS_REFERENCE.json', 'RCS初步标定结果.md', 'RCS标定说明.md'):
    raw = (output / name).read_bytes()
    m['files'][name] = dict(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
manifest_path.write_text(json.dumps(m, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
(output / 'SHA256.txt').write_text(''.join(f"{v['sha256']}  {k}\n" for k, v in m['files'].items()), encoding='utf-8')
readme = output / 'README_烧录说明.md'
text = readme.read_text(encoding='utf-8').replace(old_name, new_name)
text = '# ' + label + '\n' + text.split('\n', 1)[1]
text += '''
## 本次 RCS 默认标定

启动默认偏置为 −136.526320837 dB，来自用户提供的 4.6 dBsm 角反记录：
113 帧、雷达估计平均距离 1.18993322 m。每次重启恢复该编译默认值；
运行期间仍可使用 rcs_set_reference 重新标定，或 rcs_clear_calibration 清除。
没有新增输出协议字段，上位机不会因此新增 RCS 显示。
这是单距离初步标定，距离未独立测量；需补充多距离验证，不适用于新的 4T4R 波形。
保留原检测、CFAR、测角、后处理、波形、八通道幅相补偿和完整阵列校准流程。
force_erase=1，烧录会擦除已有配置；固件仅编译/离线验证，尚未上板验证。
'''
readme.write_text(text, encoding='utf-8')
print('PASS: linked RCS boot default, 8-channel compensation, theoretical angles, package and reference metadata')
