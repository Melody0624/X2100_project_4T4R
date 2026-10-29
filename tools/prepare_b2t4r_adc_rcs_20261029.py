"""Add the established RCS module to the exact working B ADC-fix SDK."""
from pathlib import Path
import difflib
import hashlib
import json
import shutil
import sys

project = Path(__file__).resolve().parents[1]
base = Path('/home/melody/Manhattan_Project/freertos_b2t4r_adc_fix_20261019')
build = Path('/home/melody/Manhattan_Project/freertos_b2t4r_adc_rcs_20261029')
root = project / 'artifacts/b_2t4r_adc_rcs_20261029'
current = project / 'X2100_project-main/firmware/x2100/freertos'
sys.path.insert(0, str(project / 'tools'))
from package_2t4r_tx01_compare import inputs
from b2t4r_factory_calibration import replace_defaults, CALIB

sha = lambda raw: hashlib.sha256(raw).hexdigest()
parent = json.loads((project / 'artifacts/b_2t4r_adc_fix_20261019/mips/BUILD_MANIFEST.json').read_text())
before = {n: sha(p.read_bytes()) for n, p in inputs(base).items()}
assert before == parent['source_sha256'], 'Original B SDK changed'
assert sha((base / 'zero.bin').read_bytes()) == parent['rtos_sha256']
root.mkdir(parents=True, exist_ok=True)
marker = build / '.b2t4r-adc-rcs-20261029'
if not build.exists():
    shutil.copytree(base, build)
    marker.write_text('Exact B ADC fix plus internal RCS reference\n')
assert marker.is_file(), 'Refusing to modify an unrecognized build'

updates = {}
for name in ('vendor/motor_cycle_demo/inc/rcs_estimation.h',
             'vendor/motor_cycle_demo/src/rcs_estimation.c'):
    assert name not in before
    updates[name] = (current / name).read_bytes().replace(b'\r\n', b'\n')

name = 'vendor/Makefile'
old = (base / name).read_text()
anchor = 'src-y += motor_cycle_demo/src/board_calib_config.c\n'
assert old.count(anchor) == 1
updates[name] = old.replace(anchor, anchor + 'src-y += motor_cycle_demo/src/rcs_estimation.c\n').encode()

name = 'vendor/vendor.c'
old = (base / name).read_text()
anchor = '#include "algorithm_functions.h"\n'
assert old.count(anchor) == 1
new = old.replace(anchor, anchor + '#include "rcs_estimation.h"\n')
anchor = '\t\t\t\tPERF_BEGIN(Pkt_processing);'
assert new.count(anchor) == 1
addition = ('\t\t\t\t/* Separate RCS results follow the final sorted/filtered point order. */\n'
            '\t\t\t\tif (!g_ps_cal_mode)\n'
            '\t\t\t\t\trcs_process_frame(glbCtx[last_pool_index], last_pool_index);\n'
            '\t\t\t\telse\n'
            '\t\t\t\t\trcs_clear_frame(last_pool_index);\n\n')
new = new.replace(anchor, addition + anchor)
anchor = '\t\t\t\t\tmemset(&glbCtx[i]->detInfoRD, 0, sizeof(glbCtx[i]->detInfoRD));'
assert new.count(anchor) == 1
new = new.replace(anchor, anchor + '\n\t\t\t\t\trcs_clear_frame(i);')
updates[name] = new.encode()

updates[CALIB] = replace_defaults((base / CALIB).read_text()).encode()

patch = ''
for name, raw in updates.items():
    original = (base / name).read_bytes() if (base / name).exists() else b''
    path = build / name
    assert not path.exists() or path.read_bytes() in (original, raw), name
    path.write_bytes(raw)
    saved = root / 'source_changes' / name
    saved.parent.mkdir(parents=True, exist_ok=True)
    saved.write_bytes(raw)
    patch += ''.join(difflib.unified_diff(original.decode().splitlines(True), raw.decode().splitlines(True),
                                        fromfile='B_ADC_FIX/' + name, tofile='B_ADC_RCS/' + name))
after = {n: sha(p.read_bytes()) for n, p in inputs(build).items()}
changed = sorted(n for n in before.keys() | after.keys() if before.get(n) != after.get(n))
assert changed == sorted(updates), changed
(root / 'source_snapshot.json').write_text(json.dumps(after, indent=2) + '\n')
(root / 'SOURCE_DIFF.json').write_text(json.dumps(dict(base=str(base), build=str(build),
    parent_image_sha256=parent['rtos_sha256'], changed_files=changed, inputs_checked=len(after)), indent=2) + '\n')
(root / 'rcs.patch').write_text(patch)
print('PASS: exact B ADC-fix SDK + RCS + factory array defaults; five changed source files, USB/waveform unchanged')
