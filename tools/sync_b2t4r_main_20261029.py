"""Synchronize the main Windows SDK to the verified B ADC/RCS factory source."""
from pathlib import Path
import hashlib
import json
import shutil

PROJECT = Path(__file__).resolve().parents[1]
MAIN = PROJECT / 'X2100_project-main/firmware/x2100/freertos'
ROOT = PROJECT / 'artifacts/b_2t4r_adc_rcs_20261029'
REFERENCE = ROOT / 'windows_source/freertos'
BACKUP = ROOT / 'main_sync_backup'
SKIP_DIRS = {'objs', 'symbols', '__pycache__', '.git'}
SKIP_FILES = {'zero.bin', 'zero.elf', 'zero.map', 'zero.dump.S',
              'rtos-with-spl.bin', '.config', 'config.h'}


def inventory(root):
    return {p.relative_to(root).as_posix(): p for p in root.rglob('*')
            if p.is_file() and not any(x in SKIP_DIRS for x in p.relative_to(root).parts)
            and p.name not in SKIP_FILES and p.suffix not in {'.o', '.pyc'}}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tree_hash(hashes):
    return hashlib.sha256(''.join(n + '\0' + hashes[n] + '\n' for n in sorted(hashes)).encode()).hexdigest()


if __name__ == '__main__':
    manifest = json.loads((ROOT / 'mips/BUILD_MANIFEST.json').read_text(encoding='utf-8'))
    assert sha(ROOT / 'mips/zero_ota.bin') == manifest['rtos_sha256']
    source, target = inventory(REFERENCE), inventory(MAIN)
    expected = {n: sha(p) for n, p in source.items()}
    assert all(expected[n] == digest for n, digest in manifest['source_sha256'].items())
    assert not target.keys() - source.keys(), 'Unexpected main source files; inspect before syncing'
    before = {n: sha(p) for n, p in target.items()}
    changed = sorted(n for n in target if before[n] != expected[n])
    added = sorted(source.keys() - target.keys())
    plan = dict(main=str(MAIN), reference=str(REFERENCE), backup=str(BACKUP),
                changed=changed, added=added, before_sha256={n: before[n] for n in changed})
    plan_path = ROOT / 'MAIN_SYNC_PLAN.json'
    if not plan_path.exists():
        plan_path.write_bytes((json.dumps(plan, ensure_ascii=False, indent=2) + '\n').encode())
    for name in changed:
        saved = BACKUP / name
        saved.parent.mkdir(parents=True, exist_ok=True)
        if saved.exists():
            assert sha(saved) == before[name], 'Previous backup differs: ' + name
        else:
            shutil.copy2(target[name], saved)
    for name in changed + added:
        destination = MAIN / name
        destination.resolve().relative_to(MAIN.resolve())
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source[name], destination)
    actual = {n: sha(p) for n, p in inventory(MAIN).items()}
    assert actual == expected, 'Main SDK is not byte-for-byte identical'
    template = PROJECT / 'configs/b_2t4r_adc_rcs_factory_20261029_defconfig'
    template.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(REFERENCE / '.config.in', template)
    result = dict(configuration=manifest['cfg_file'], image_sha256=manifest['rtos_sha256'],
                  main_source='X2100_project-main/firmware/x2100/freertos',
                  reference_source='artifacts/b_2t4r_adc_rcs_20261029/windows_source/freertos',
                  matching_source_and_asset_files=len(actual),
                  matching_build_inputs=len(manifest['source_sha256']),
                  byte_identical=True, source_tree_sha256=tree_hash(actual),
                  configuration_template=template.relative_to(PROJECT).as_posix(),
                  changed_files_this_run=changed, added_files_this_run=added,
                  excluded='Generated objects, images, generated config.h/.config and caches')
    result_path = PROJECT / 'docs/b2t4r_adc_rcs_factory_main_sync_20261029.json'
    if not changed and not added and result_path.exists():
        previous = json.loads(result_path.read_text(encoding='utf-8'))
        result['changed_files_this_run'] = previous['changed_files_this_run']
        result['added_files_this_run'] = previous['added_files_this_run']
    result_path.write_bytes((json.dumps(result, ensure_ascii=False, indent=2) + '\n').encode())
    print('PASS:', len(actual), 'source/asset files and', len(manifest['source_sha256']),
          'build inputs match byte-for-byte; differences backed up; generated products excluded')
