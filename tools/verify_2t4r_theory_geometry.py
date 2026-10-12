"""Verify the two theory builds against the confirmed phase-center geometry."""
from pathlib import Path
import hashlib
import json
import math
import struct
import sys

from verify_vendor2t4r_tx01_cal import VALUES, symbol_bytes


def check(root,arraycal=False):
    suffix='arraycal' if arraycal else 'theory'
    outputs = [root / name for name in ('A_vendor_new_'+suffix, 'B_updated_source_'+suffix)]
    pools = []
    spacing = -0.00196 * 76.5e9 / 299792458.0
    for out in outputs:
        m = json.loads((out / 'BUILD_MANIFEST.json').read_text(encoding='utf-8'))
        assert m['angle_mapping'] == ('theory_defaults_with_saved_calibration' if arraycal else 'theoretical_from_spacing')
        assert m['antenna_geometry']['signed_spacing_mm'] == -1.96
        assert m['antenna_geometry']['used_tx_indices'] == [0, 1]
        build = Path(m['build_sdk'])
        for name, digest in m['build_patch_sha256'].items():
            assert hashlib.sha256((build / name).read_bytes()).hexdigest() == digest, name
        header = (build / 'vendor/motor_cycle_demo/inc/radar_types.h').read_text()
        switch = next(x for x in header.splitlines() if x.startswith('#define') and 'IS_ANG_ACC_IMPROVE_INTERP' in x)
        assert switch.split()[2] == ('true' if arraycal else 'false')
        params = (build / 'vendor/motor_cycle_demo/src/set_params.c').read_text()
        assert 'minElemSpacing[0] = -0.00196f / glbCtx->waveParams.wavelength[0];' in params
        pools.append((build / 'vendor/motor_cycle_demo/src/memory_pool.c').read_bytes())
        elf = (out / 'zero.elf').read_bytes()
        assert symbol_bytes(elf, 'angCalibMat') == struct.pack('<16f', *VALUES)
        if arraycal:
            angles=struct.unpack('<128f',symbol_bytes(elf,'angFFT_interp'))
            for i,value in enumerate(angles):
                expected=math.degrees(math.asin((i if i<64 else i-128)/(128*spacing)))
                assert abs(value-expected)<1e-5,(i,value,expected)
            assert symbol_bytes(elf,'mappingGen_processing_with_spacing')
        positions = struct.unpack('<8f', symbol_bytes(elf, 'vxPosn_sf1_azm'))
        assert all(abs(value - i * spacing) < 1e-6 for i, value in enumerate(positions))
        assert math.isfinite(spacing) and spacing < 0
        print(f'PASS {out.name}: {"theoretical defaults and saved calibration enabled" if arraycal else "factory table disabled"}, document spacing {spacing:.9f} wavelengths, exact user matrix and linked 8-element positions')
    assert pools[0] == pools[1], 'A and B must generate angle tables using identical initialization code'
    log = (outputs[1] / 'tests/pipeline.log').read_text()
    assert 'PASS: actual SDK angle mapping matches all 128 theoretical values' in log
    assert 'PASS: real SDK initialization/reinitialization, 6 full ADC' in log
    if arraycal:
        assert 'PASS: source calibration hooks, zero-phase save, all angle samples including last, signed mapping save and immediate application' in log
    print('PASS shared A/B angle-table initialization and B ASan/UBSan DSP regression; hardware results pending')


if __name__ == '__main__':
    check(Path(sys.argv[1]),'--array-cal' in sys.argv[2:])
