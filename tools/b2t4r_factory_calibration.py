"""Import only the factory BARRACADE amplitude/phase and angle defaults."""
from pathlib import Path
import hashlib
import json
import re
import sys

PROJECT = Path(__file__).resolve().parents[1]
REFERENCE = PROJECT / 'docs/b2t4r_factory_calibration_20261029.json'
MARKER = '#elif (BOARD_TYPE == BOARD_TYPE_BARRACADE)'
CALIB = 'vendor/motor_cycle_demo/src/board_calib_config.c'


def array(text, name):
    start = text.index(MARKER) if name == 'angCalibMat' else text.rindex(MARKER)
    match = re.search(r'\b' + name + r'\[\d+\]\s*=\s*\{.*?\};', text[start:], re.S)
    assert match, name
    body = match.group().split('=', 1)[1]
    body = re.sub(r'/\*.*?\*/|//[^\n]*', '', body, flags=re.S)
    return [float(v) for v in re.findall(r'([-+]?\d+\.\d+)f', body)]


def replace_defaults(text):
    reference = json.loads(REFERENCE.read_text())
    for name, type_, count, width in (('angCalibMat', 'fft_cpx_f32', 8, 2),
                                      ('angFFT_interp', 'float', 128, 8)):
        values = reference[name]
        assert len(values) == (16 if name == 'angCalibMat' else 128)
        start = text.index(MARKER) if name == 'angCalibMat' else text.rindex(MARKER)
        match = re.search(r'\b' + name + r'\[\d+\]\s*=\s*\{.*?\};', text[start:], re.S)
        assert match
        rows = []
        for i in range(0, len(values), width):
            row = ', '.join(f'{v:.12f}f' for v in values[i:i+width])
            rows.append('    ' + ('{ ' + row + ' }' if width == 2 else row) + ',')
        declaration = (MARKER + '\n/* Factory BARRACADE defaults, restored 2026-10-29.\n'
                       ' * Valid saved or active array calibration may override these values. */\n'
                       f'{type_} {name}[{count}] = {{\n' + '\n'.join(rows) + '\n};')
        text = text[:start] + declaration + text[start + match.end():]
        assert array(text, name) == values
    return text


if __name__ == '__main__':
    if sys.argv[1:] == ['--capture-vendor']:
        source = PROJECT / 'MotorCycle_Ingenic/MotorCycle_Ingenic/freertos' / CALIB
        raw = source.read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        assert digest == 'f14cf2a0bfe487db294eee9e5317a45574267c03d60de1308128bf4b1d9b2d9e'
        data = dict(source=source.relative_to(PROJECT).as_posix(), source_sha256=digest,
                    board='BOARD_ANTENNA_2T4R / BOARD_TYPE_BARRACADE',
                    angCalibMat=array(raw.decode(), 'angCalibMat'),
                    angFFT_interp=array(raw.decode(), 'angFFT_interp'))
        assert len(data['angCalibMat']) == 16 and len(data['angFFT_interp']) == 128
        output = json.dumps(data, ensure_ascii=False, indent=2) + '\n'
        assert not REFERENCE.exists() or REFERENCE.read_text() == output
        REFERENCE.write_bytes(output.encode('utf-8'))
        print('PASS: captured factory eight complex coefficients and 128 angle values')
    elif sys.argv[1:] == ['--update-windows']:
        path = PROJECT / 'X2100_project-main/firmware/x2100/freertos' / CALIB
        old = path.read_bytes()
        new = replace_defaults(old.decode().replace('\r\n', '\n'))
        backup = PROJECT / 'artifacts/b_2t4r_adc_rcs_20261029/windows_previous' / CALIB
        backup.parent.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            backup.write_bytes(old)
        path.write_bytes(new.replace('\n', '\r\n' if b'\r\n' in old else '\n').encode())
        print('PASS: Windows B defaults now use factory amplitude/phase and angle tables')
    else:
        raise SystemExit('Use --capture-vendor or --update-windows')
