"""Package and verify the source-retaining 2T4R SDK after a real WSL build."""
from pathlib import Path
import configparser
import difflib
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys

from verify_vendor2t4r_tx01_cal import symbol_bytes
from test_vendor2t4r_source_update import check
from update_vendor2t4r_source import read


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    project, source, build, output, compiler = map(Path, sys.argv[1:6])
    artifact = output.parent
    latest = project / 'MotorCycle_Ingenic/MotorCycle_Ingenic/freertos'
    backup = artifact / 'original_sdk'
    spl = 'x2100l-spl-sfc-nor-ota-1200M-500M.bin'
    cfg_name = 'vendor2t4r_source_2M_ota_20261010.cfg'
    # check() uses project-relative paths; pass the real updated SDK explicitly.
    check(source)
    excluded = {'objs', 'symbols', '__pycache__', 'zero.bin', 'zero.elf',
                'zero.map', 'zero.dump.S', 'rtos-with-spl.bin', '.config', 'config.h'}
    text_suffixes = {'.c', '.h', '.S', '.mk', '.sh', '.py', '.in', '.lds'}
    hashes = {}
    for path in source.rglob('*'):
        relative = path.relative_to(source)
        if not path.is_file() or any(part in excluded for part in relative.parts):
            continue
        if path.suffix not in text_suffixes | {'.a'} and path.name != 'Makefile' and not path.name.endswith('_defconfig'):
            continue
        raw, copied = path.read_bytes(), (build / relative).read_bytes()
        if relative.as_posix() == '.config.in':
            expected = '\n'.join('RTOS_TOOLCHAIN_PATH=' + str(compiler)
                if line.startswith('RTOS_TOOLCHAIN_PATH=') else line
                for line in raw.decode().splitlines()) + '\n'
            assert copied == expected.encode(), relative
        else:
            assert copied == (raw if path.suffix == '.a' else raw.replace(b'\r', b'')), relative
        hashes[relative.as_posix()] = sha(path)
    hashes['xburst2/soc-x2000/spl/' + spl] = sha(source / 'xburst2/soc-x2000/spl' / spl)

    core = ['detection_processing.c', 'cfar_detection.c', 'cfar_func_v2.c',
            'angle_estimation_v1.c', 'detection_post_processing.c']
    log = (output / 'build.log').read_text(errors='replace')
    builtin_link = next(line for line in log.splitlines() if '-r -o vendor/builtin.o ' in line)
    for name in core:
        assert '/src/detection/' + name + '.o' in builtin_link, name
        relative = Path('vendor/motor_cycle_demo/src/detection') / name
        original = read(backup / relative)
        updated = (source / relative).read_text()
        (artifact / 'detection_changes' / (name + '.patch')).write_text(''.join(
            difflib.unified_diff(original.splitlines(True), updated.splitlines(True),
                                 fromfile='old/' + name, tofile='updated/' + name)))
    final_link = next(line for line in reversed(log.splitlines()) if line.endswith('-o zero.elf'))
    assert '-lmmw_detection' not in final_link and '-lmmw_velocity' in final_link
    velocity = source / 'vendor/motor_cycle_demo/mmwlib/libmmw_velocity.a'
    members = subprocess.check_output([str(compiler / 'mips-sde-elf-ar'), 't', str(velocity)], text=True).splitlines()
    assert members == ['velocity_disambiguation.o'], members
    extracted = subprocess.check_output([str(compiler / 'mips-sde-elf-ar'), 'p', str(velocity), members[0]])
    latest_object = subprocess.check_output([str(compiler / 'mips-sde-elf-ar'), 'p',
        str(latest / 'vendor/motor_cycle_demo/mmwlib/libmmw_detection.a'), members[0]])
    assert extracted == latest_object
    assert sha(source / 'vendor/motor_cycle_demo/mmwlib/libmmw_tracking.a') == sha(latest / 'vendor/motor_cycle_demo/mmwlib/libmmw_tracking.a')

    elf = (output / 'zero.elf').read_bytes()
    matrix = symbol_bytes(elf, 'angCalibMat')
    calibration = (source / 'vendor/motor_cycle_demo/src/board_calib_config.c').read_text()
    section = calibration.split('#elif (BOARD_TYPE == BOARD_TYPE_BARRACADE)', 1)[1].split('\n#else', 1)[0]
    pairs = re.findall(r'\{\s*([-+\d.eE]+)f?\s*,\s*([-+\d.eE]+)f?\s*\}', section)
    assert len(pairs) == 8
    assert matrix == struct.pack('<16f', *(float(value) for pair in pairs for value in pair))

    cloner = project / 'MotorCycle_Ingenic/MotorCycle_Ingenic/tools/USBCloner/cloner-latest-windows/cloner-latest-windows/cloner-2.5.58.5-windows_alpha/cloner-2.5.58.5-windows_alpha'
    protected = json.loads((project / 'artifacts/vendor2t4r_2M_ota_20261009/protected_cloner_bin_sha256.json').read_text())
    for name, digest in protected.items():
        assert sha(cloner / name) == digest, 'Protected Cloner file changed: ' + name

    cfg = configparser.ConfigParser(interpolation=None)
    cfg.optionxform = str
    cfg.read(project / 'MotorCycle_Ingenic/MotorCycle_Ingenic/tools/USBCloner/common/configs/x2100/x2100x_cheetah_sfc_nor_2M_ota_freertos.cfg')
    windows_output = output.as_posix().replace('/mnt/d/', 'D:/', 1)
    for index, name in enumerate((spl, 'zero_ota.bin', 'zero_ota.bin')):
        cfg[f'policy{index}']['attribute'] = windows_output + '/' + name
        cfg[f'policy{index}']['enabled'] = '1'
    for index in (3, 4):
        cfg[f'policy{index}']['enabled'] = '0'
        cfg[f'policy{index}']['attribute'] = ''
    cfg['sfc']['force_erase'] = '1'
    with (output / cfg_name).open('w') as stream:
        cfg.write(stream, space_around_delimiters=False)

    manifest = {
        'date': '2026-10-10', 'source_sdk': str(source), 'build_sdk': str(build),
        'cfg_file': cfg_name, 'source_sha256': hashes, 'source_detection': True,
        'source_detection_files': core, 'velocity_archive_members': members,
        'linked_calibration_hex': matrix.hex(), 'force_erase': True,
        'optimization': '-Os', 'rtos_slot_bytes': 0xEA000,
        'spl_origin': 'vendor prebuilt OTA SPL; not rebuilt from source',
        'hardware_tested': False, 'protected_cloner_sha256': protected,
        'board': 'X2100L / 2T4R / 2 MiB NOR', 'files': {},
    }
    for name in ('zero_ota.bin', spl, 'zero.elf', 'build.config', cfg_name):
        path = output / name
        manifest['files'][name] = {'bytes': path.stat().st_size, 'sha256': sha(path)}
    (output / 'BUILD_MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'SHA256.txt').write_text(''.join(f"{item['sha256']}  {name}\n" for name, item in manifest['files'].items()))
    shutil.copyfile(output / 'zero_ota.bin', source / 'zero.bin')
    print('PASS: compiled source detection ownership, isolated velocity object, build/source identity, calibration and protected Cloner bins')


if __name__ == '__main__':
    main()
