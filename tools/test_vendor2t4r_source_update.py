"""Verify source detection ownership and the updated SDK against its provenance."""
from pathlib import Path
import argparse
import re

PROJECT = Path(__file__).resolve().parents[1]
TARGET = PROJECT / 'X2100_project-main/firmware/x2100/freertos'
NEW = PROJECT / 'MotorCycle_Ingenic/MotorCycle_Ingenic/freertos'


def normalized(path):
    return path.read_bytes().replace(b'\r', b'')


def check(root):
    vendor = root / 'vendor'
    types = normalized(vendor / 'motor_cycle_demo/inc/radar_types.h').decode('utf-8')
    assert re.search(r'#define\s+BOARD_ANTENNA_CONFIG\s+BOARD_ANTENNA_2T4R', types)
    assert re.search(r'#define\s+NUM_VX_SF1\s+8\b', types), 'must remain 2TX x 4RX'
    for name in ['detection_processing.c', 'cfar_detection.c', 'cfar_func_v2.c',
                 'angle_estimation_v1.c', 'detection_post_processing.c']:
        path = vendor / 'motor_cycle_demo/src/detection' / name
        assert path.is_file(), f'missing source: {name}'
    build = normalized(vendor / 'Makefile').decode('utf-8')
    link = normalized(root / 'package/vendor/vendor.mk').decode('utf-8')
    assert 'CONFIG_USE_DETECTION_LIB := n' in build
    assert '-lmmw_detection' not in link, 'closed detection library must not be linked'
    params = normalized(vendor / 'motor_cycle_demo/src/set_params.c').decode('utf-8')
    assert '16.699e12f' in params, 'new waveform slope missing'
    expected = normalized(NEW / 'vendor/vendor.c').replace(
        b'g_ps_cal_saved_chirps = glbCtx[0]->waveParams.numChirps;',
        b'g_ps_cal_saved_chirps = glbCtx[0]->waveParams.numChirps[0];').replace(
        b'g_ps_cal_saved_adc_len = glbCtx[0]->waveParams.adcLen;',
        b'g_ps_cal_saved_adc_len = glbCtx[0]->waveParams.adcLen[0];')
    assert normalized(vendor / 'vendor.c').rstrip(b'\n') == expected.rstrip(b'\n')
    adapted = {'.config.in', 'vendor/Makefile', 'package/vendor/vendor.mk',
               'vendor/vendor.c', 'vendor/motor_cycle_demo/inc/radar_functions.h',
               'vendor/motor_cycle_demo/src/set_params.c'}
    adapted.update({'vendor/InitPhaseCalib/InitPhaseCalib.c',
                    'vendor/InitPhaseCalib/mapping_gen_processing.c',
                    'vendor/InitPhaseCalib/mapping_gen_processing.h'})
    generated = {'objs', 'symbols', '__pycache__', 'zero.bin', 'zero.elf',
                 'zero.map', 'zero.dump.S', 'rtos-with-spl.bin', '.config', 'config.h'}
    for path in NEW.rglob('*'):
        relative = path.relative_to(NEW)
        if not path.is_file() or any(part in generated for part in relative.parts) or path.suffix in {'.o', '.log'}:
            continue
        if relative.as_posix() in adapted:
            continue
        target = root / relative
        assert target.is_file(), f'missing new SDK file: {relative}'
        normalize_text = path.suffix in {'.c', '.h', '.mk', '.in', '.sh', '.py'} or path.name == 'Makefile'
        assert (normalized(target) == normalized(path) if normalize_text else target.read_bytes() == path.read_bytes()), relative
    print('PASS: new waveform, 2T4R, source detection and new application')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('root', nargs='?', type=Path, default=TARGET)
    args = parser.parse_args()
    check(args.root)
