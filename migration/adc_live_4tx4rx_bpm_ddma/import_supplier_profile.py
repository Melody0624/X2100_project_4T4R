"""Extract supplier conditional table; never invent or decode RF bits."""
import hashlib
import json
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1])
raw = source.read_bytes()
try:
    content = raw.decode('utf-8-sig')
except UnicodeDecodeError:
    content = raw.decode('gb18030')
marker = '#elif (CHEETAH_CONFIG == cheetah_128_512_config)'
start = content.index(marker) + len(marker)
depth, offset, end = 0, start, None
for line in content[start:].splitlines(keepends=True):
    directive = re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b', line)
    if directive:
        word = directive.group(1)
        if depth == 0 and word in ('elif', 'else', 'endif'):
            end = offset
            break
        depth += 1 if word in ('if', 'ifdef', 'ifndef') else -1 if word == 'endif' else 0
    offset += len(line)
if end is None:
    raise RuntimeError('Cannot find end of supplier branch')
table = content[start:end].strip()
assert table.count('static struct reg_line cheetah_default_config[]') == 1
table = table.replace('static struct reg_line cheetah_default_config[]',
                      'static const struct reg_line supplier_registers[]')
macros = ['IS_MIRROR', 'CHEETAH_SAVE', 'SAVE_RAW_DATA', 'ADC_REPLAY',
          'USE_USB_OUTPUT', 'UART_ADC_SEND', 'USE_BPM']
for name in macros:
    table = re.sub(r'\b' + name + r'\b', 'SUPPLIER_' + name, table)
root = pathlib.Path(__file__).resolve().parent
generated = ('/* Generated from vendor_bsis.c: cheetah_128_512_config.\n'
             ' * Register words/order preserved; only symbol names changed. */\n' + table + '\n')
(root / 'supplier_registers.inc').write_text(generated, encoding='utf-8')
record = dict(source_name=source.name, source_sha256=hashlib.sha256(raw).hexdigest(),
              branch=marker, generated_sha256=hashlib.sha256(generated.encode()).hexdigest(),
              renamed_macros=macros, hardware_validated=False)
(root / 'supplier_provenance.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
print(json.dumps(record, indent=2))
