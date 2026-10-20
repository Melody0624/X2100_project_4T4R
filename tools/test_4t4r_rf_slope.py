"""Check the real 4TX table against the supplied baseline, preserving other bits."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / 'X2100_project-main/firmware/x2100/freertos'
TABLE = SDK / 'vendor/cheetah/source4t4r_register_config.h'
BASE = ROOT / 'migration/adc_live_4tx4rx_bpm_ddma/supplier_registers.inc'

def rows(text, bpm, raw):
    values = dict(IS_MIRROR=0, CHEETAH_SAVE=0, SAVE_RAW_DATA=0, ADC_REPLAY=0,
                  USE_USB_OUTPUT=1, UART_ADC_SEND=raw, USE_BPM=bpm)
    prefix = '#define CMD_DELAY -1\n#define CMD_RESET_RAMS -2\n'
    for name, value in values.items():
        prefix += f'#define {name} {value}\n#define SUPPLIER_{name} {value}\n'
    parsed = subprocess.run(['gcc', '-E', '-P', '-x', 'c', '-'],
                            input=prefix+text, text=True, capture_output=True, check=True).stdout
    result = []
    for chip, addr, count, payload in re.findall(
            r'\{\s*(\d+)\s*,\s*(-?0x[0-9A-Fa-f]+|-?\d+)\s*,\s*(\d+)\s*,\s*\{([^}]+)\}\s*\}', parsed):
        words = [int(x.strip(), 0) for x in payload.split(',')]
        assert len(words) == int(count) <= 64
        result.append((int(chip), int(addr, 0), int(count), words))
    return result

def main():
    assert TABLE.exists(), 'Four-TX normal register table has not been integrated'
    vendor = (SDK / 'vendor/vendor.c').read_text()
    assert '#include "cheetah/source4t4r_register_config.h"' in vendor
    source = TABLE.read_text()
    old = BASE.read_text()
    for bpm, raw in ((1, 0), (0, 0), (1, 1)):
        before, after = rows(old, bpm, raw), rows(source, bpm, raw)
        assert len(after) == len(before) and len(after) > 80
        differences = []
        for index, (a, b) in enumerate(zip(before, after)):
            assert a[:3] == b[:3], (index, a[:3], b[:3])
            for word, (x, y) in enumerate(zip(a[3], b[3])):
                if x != y:
                    differences.append((a[1], word, x, y))
        assert differences == [(0x18, 0, 0x0EE6C000, 0x8EE6C000),
                               (0x18, 1, 0x08206064, 0x08206055)], differences
        p = next(row[3][:3] for row in after if row[1] == 0x18)
        step = (p[0] >> 31) | ((p[1] & 0x1fff) << 1)
        assert step == 0xAB and step * 0.09765625 == 16.69921875
        # FSTART, all timing fields and sample count must remain unchanged.
        assert (p[0] & 0x7fffffff) == 0x0EE6C000
        assert (p[1] & ~0x1fff) == (0x08206064 & ~0x1fff)
        assert p[2] == 0x001F905C
        print(f'PASS: {len(after)} rows, BPM={bpm}, ADC output={raw}; only FSTEP bits changed; slope=16.69921875 MHz/us')

if __name__ == '__main__':
    main()
