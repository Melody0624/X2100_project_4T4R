"""Create clearly named OTA packages with the supplied eight-channel defaults."""
from pathlib import Path
import configparser
import difflib
import math
import hashlib
import json
import re
import struct
import sys

from verify_vendor2t4r_tx01_cal import VALUES, symbol_bytes

PROJECT = Path(__file__).resolve().parents[1]
SPL = 'x2100l-spl-sfc-nor-ota-1200M-500M.bin'
CALIB = 'vendor/motor_cycle_demo/src/board_calib_config.c'
LABELS = {
    'A_vendor_new': 'A：厂家新版 2T4R · 用户八通道标定版',
    'B_updated_source': 'B：更新后源码算法 2T4R · 用户八通道标定版',
    'B_updated_source_windowfix': 'B修正版：源码算法 2T4R · 用户八通道标定 · 修复启动窗函数空指针',
    'A_vendor_new_theory': 'A：厂家新版 2T4R · 用户八通道标定 · 理论角度版',
    'B_updated_source_theory': 'B：源码算法 2T4R · 用户八通道标定 · 理论角度版（含启动修复）',
    'A_vendor_new_arraycal': 'A：厂家新版 2T4R · 阵列校准 · 用户八通道与理论角度默认值',
    'B_updated_source_arraycal': 'B：源码算法 2T4R · 恢复阵列校准 · 用户八通道与理论角度默认值',
}
EXCLUDED = {'objs', 'symbols', '__pycache__', 'zero.bin', 'zero.elf', 'zero.map',
            'zero.dump.S', 'rtos-with-spl.bin', '.config', 'config.h'}
TEXT = {'.c', '.h', '.S', '.mk', '.sh', '.py', '.in', '.lds'}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def inputs(source):
    return {p.relative_to(source).as_posix(): p for p in source.rglob('*')
            if p.is_file() and not any(part in EXCLUDED for part in p.relative_to(source).parts)
            and (p.suffix in TEXT | {'.a'} or p.name == 'Makefile' or p.name.endswith('_defconfig'))}


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode('utf-8'))


def prepare(source, build, output, compiler, variant):
    source_files = inputs(source)
    snapshot = {name: sha(path.read_bytes()) for name, path in source_files.items()}
    snapshot['xburst2/soc-x2000/spl/' + SPL] = sha((source / 'xburst2/soc-x2000/spl' / SPL).read_bytes())
    write(output / 'source_snapshot.json', json.dumps(snapshot, indent=2) + '\n')
    for name, path in source_files.items():
        if path.suffix != '.a':
            (build / name).write_bytes(path.read_bytes().replace(b'\r', b''))

    changes = {}
    arraycal = variant.endswith('_arraycal')
    if variant.endswith('_theory') or arraycal:
        name = 'vendor/motor_cycle_demo/inc/radar_types.h'
        original = (build / name).read_text()
        modified, count = re.subn(r'(#define\s+IS_ANG_ACC_IMPROVE_INTERP\s+)true\b',
                                 r'\g<1>' + ('true' if arraycal else 'false'), original)
        assert count == 1, 'Expected the factory angle table switch exactly once'
        changes[name] = (original, modified)
        name = 'vendor/motor_cycle_demo/src/set_params.c'
        original = (build / name).read_text()
        anchor = '\tif (init_memory_pool(glbCtx, pool_index, 0, 0) != 0) {'
        assert original.count(anchor) == 1
        addition = ('\t/* User phase-center document: TX0/RX0 are rightmost,\n'
                    '\t * TX-major virtual channels advance left by 1.96 mm. */\n'
                    '\tglbCtx->basicParams.minElemSpacing[0] = -0.00196f / glbCtx->waveParams.wavelength[0];\n\n')
        changes[name] = (original, original.replace(anchor, addition + anchor))
        name = 'vendor/motor_cycle_demo/src/memory_pool.c'
        original = (build / name).read_text()
        spacing = -0.00196 * 76.5e9 / 299792458.0
        positions = ', '.join(f'{i*spacing:.9f}f' for i in range(8))
        modified, count = re.subn(r'(static float vxPosn_sf1_azm\[NUM_VX_AZM_SF1\] = )\{[^}]*\};',
                                 r'\g<1>{ ' + positions + ' };', original)
        assert count == 1
        changes[name] = (original, modified)
    original = (build / CALIB).read_text()
    start = original.index('#elif (BOARD_TYPE == BOARD_TYPE_BARRACADE)')
    end = original.index('\n#else', start)
    rows = '\n'.join(f'    {{ {VALUES[i*2]:.6f}f, {VALUES[i*2+1]:.6f}f }}, /* TX{i//4}RX{i%4} */' for i in range(8))
    replacement = '#elif (BOARD_TYPE == BOARD_TYPE_BARRACADE)\n/* User TX0/TX1 x 4RX compensation; 2026-10-10. */\nfft_cpx_f32 angCalibMat[8] = {\n' + rows + '\n};'
    changes[CALIB] = (original, original[:start] + replacement + original[end:])
    if arraycal:
        old, modified = changes[CALIB]
        start = modified.rindex('#elif (BOARD_TYPE == BOARD_TYPE_BARRACADE)')
        end = modified.index('\n#endif /* BOARD_TYPE */', start)
        spacing = -0.00196 * 76.5e9 / 299792458.0
        angles = [math.degrees(math.asin(max(-1, min(1, (i if i<64 else i-128)/(128*spacing))))) for i in range(128)]
        table = '\n'.join('    ' + ', '.join(f'{x:.9f}f' for x in angles[i:i+8]) + ',' for i in range(0,128,8))
        modified = modified[:start] + '#elif (BOARD_TYPE == BOARD_TYPE_BARRACADE)\n/* User geometry theoretical defaults; valid saved calibration may override. */\nfloat angFFT_interp[128] = {\n' + table + '\n};' + modified[end:]
        changes[CALIB] = (old, modified)
        restored = PROJECT / 'X2100_project-main/firmware/x2100/freertos'
        for name in ('vendor/InitPhaseCalib/InitPhaseCalib.c',
                     'vendor/InitPhaseCalib/mapping_gen_processing.c',
                     'vendor/InitPhaseCalib/mapping_gen_processing.h'):
            old = (build / name).read_text()
            new = (restored / name).read_bytes().replace(b'\r',b'').decode('utf-8')
            if old != new: changes[name] = (old,new)

    name = 'vendor/vendor.c'
    original = (build / name).read_text()
    modified = original.replace('g_ps_cal_saved_chirps = glbCtx[0]->waveParams.numChirps;',
                                'g_ps_cal_saved_chirps = glbCtx[0]->waveParams.numChirps[0];').replace(
        'g_ps_cal_saved_adc_len = glbCtx[0]->waveParams.adcLen;',
        'g_ps_cal_saved_adc_len = glbCtx[0]->waveParams.adcLen[0];')
    if modified != original:
        changes[name] = (original, modified)

    original = (build / '.config.in').read_text()
    settings = {'RTOS_TOOLCHAIN_PATH': str(compiler), 'CONFIG_OS_GCC_OPTIMIZATION': '-Os',
                'CONFIG_SPL_BIN_FILE': 'xburst2/soc-x2000/spl/' + SPL}
    lines = original.splitlines()
    for key, value in settings.items():
        indices = [i for i, line in enumerate(lines) if line.startswith(key + '=')]
        assert len(indices) == 1, key
        lines[indices[0]] = key + '=' + value
    changes['.config.in'] = (original, '\n'.join(lines) + '\n')
    patches = []
    for name, (old, new) in changes.items():
        write(build / name, new)
        write(output / 'source_changes' / name, new)
        patches.extend(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                                          fromfile='base/' + name, tofile=variant + '/' + name))
    write(output / 'firmware_changes.patch', ''.join(patches))
    write(output / 'build_patch_sha256.json', json.dumps({name: sha(new.encode()) for name, (_, new) in changes.items()}, indent=2) + '\n')


def finish(source, build, output, compiler, variant):
    snapshots = json.loads((output / 'source_snapshot.json').read_text())
    patched = json.loads((output / 'build_patch_sha256.json').read_text())
    for name, digest in snapshots.items():
        raw = (source / name).read_bytes()
        assert sha(raw) == digest, 'Input changed during build: ' + name
        copied = (build / name).read_bytes()
        if name in patched:
            assert sha(copied) == patched[name], name
        else:
            assert copied == (raw if name.endswith(('.a', '.bin')) else raw.replace(b'\r', b'')), name

    expected = struct.pack('<16f', *VALUES)
    assert symbol_bytes((output / 'zero.elf').read_bytes(), 'angCalibMat') == expected
    assert expected in (output / 'zero_ota.bin').read_bytes()
    log = (output / 'build.log').read_text(errors='replace')
    link = next(line for line in reversed(log.splitlines()) if line.endswith('-o zero.elf'))
    source_detection = variant.startswith('B_updated_source')
    if source_detection:
        assert '-lmmw_detection' not in link and '-lmmw_velocity' in link
        builtin = next(line for line in log.splitlines() if '-r -o vendor/builtin.o ' in line)
        for name in ('detection_processing', 'cfar_detection', 'cfar_func_v2', 'angle_estimation_v1', 'detection_post_processing'):
            assert '/src/detection/' + name + '.c.o' in builtin, name
    else:
        assert '-lmmw_detection' in link and '-lmmw_velocity' not in link

    cfg_name = ({'A_vendor_new_theory': 'A_2T4R_theory_8ch.cfg',
                 'B_updated_source_theory': 'B_2T4R_theory_8ch.cfg',
                 'A_vendor_new_arraycal': 'A_2T4R_arraycal_8ch.cfg',
                 'B_updated_source_arraycal': 'B_2T4R_arraycal_8ch.cfg'}.get(variant)
                or variant + '_2T4R_TX01_8CH_2M_OTA_20261010.cfg')
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.optionxform = str
    cfg.read(PROJECT / 'MotorCycle_Ingenic/MotorCycle_Ingenic/tools/USBCloner/common/configs/x2100/x2100x_cheetah_sfc_nor_2M_ota_freertos.cfg')
    win_output = output.as_posix().replace('/mnt/d/', 'D:/', 1)
    for index, name in enumerate((SPL, 'zero_ota.bin', 'zero_ota.bin')):
        cfg[f'policy{index}']['enabled'] = '1'
        cfg[f'policy{index}']['attribute'] = win_output + '/' + name
    for index in (3, 4):
        cfg[f'policy{index}']['enabled'] = '0'
        cfg[f'policy{index}']['attribute'] = ''
    cfg['sfc']['force_erase'] = '1'
    with (output / cfg_name).open('w', encoding='utf-8') as stream:
        stream.write('; ' + LABELS[variant] + '\n')
        cfg.write(stream, space_around_delimiters=False)

    manifest = {'date': '2026-10-12' if variant.endswith('_arraycal') else '2026-10-10', 'name': LABELS[variant], 'variant': variant,
        'source_sdk': str(source), 'build_sdk': str(build), 'cfg_file': cfg_name,
        'source_sha256': snapshots, 'build_patch_sha256': patched,
        'linked_calibration_hex': expected.hex(), 'compensation_real_imag': list(zip(VALUES[::2], VALUES[1::2])),
        'source_detection': source_detection, 'force_erase': True, 'hardware_tested': False,
        'angle_mapping': 'theory_defaults_with_saved_calibration' if variant.endswith('_arraycal') else ('theoretical_from_spacing' if variant.endswith('_theory') else 'factory_interp'),
        'rtos_slot_bytes': 0xEA000, 'optimization': '-Os',
        'calibration_policy': 'compiled user defaults; valid Flash matrix may override after later calibration',
        'spl_origin': 'vendor prebuilt OTA SPL; not rebuilt from source', 'files': {}}
    if variant.endswith('_theory') or variant.endswith('_arraycal'):
        manifest['antenna_geometry'] = {
            'source': 'user phase-center document, right-to-left TX1..4/RX1..4 confirmed',
            'used_tx_indices': [0, 1], 'rx_position_mm': [5.88, 3.92, 1.96, 0.0],
            'tx_position_mm': [31.3612, 23.5212],
            'virtual_position_mm': [tx + rx for tx in [31.3612, 23.5212] for rx in [5.88, 3.92, 1.96, 0.0]],
            'signed_spacing_mm': -1.96, 'center_hz': 76.5e9,
            'signed_spacing_wavelengths': -0.00196 * 76.5e9 / 299792458.0}
    for name in ('zero_ota.bin', SPL, 'zero.elf', 'build.config', 'firmware_changes.patch', cfg_name):
        raw = (output / name).read_bytes()
        manifest['files'][name] = {'bytes': len(raw), 'sha256': sha(raw)}
    write(output / 'BUILD_MANIFEST.json', json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    write(output / 'SHA256.txt', ''.join(f"{item['sha256']}  {name}\n" for name, item in manifest['files'].items()))
    render_readme(output, variant, manifest)
    if source_detection:
        note = '\n## 启动崩溃修复\n\n本包补齐源码检测所需的距离、多普勒和测角窗初始化，修复原 B 包第 2 帧的 `winRng_sf1=NULL` 崩溃。原 B 包 USB 能枚举，但固件随后 `Game over` 导致主机连接卡住。详见 `docs/2t4r_B_windowfix_20261010.md`。尚需上板复测。\n'
        readme = output / 'README_烧录说明.md'
        write(readme, readme.read_text(encoding='utf-8') + note)
    print('PASS: exact user matrix in ELF and payload; variant link ownership; input/build hashes')


def render_readme(output, variant, manifest):
    source_detection = manifest['source_detection']
    cfg_name = manifest['cfg_file']
    win_output = output.as_posix().replace('/mnt/d/', 'D:/', 1)
    algorithm = ('保留旧版检测、CFAR、测角、后处理源码并适配新版，其余采用新版。'
                 if source_detection else '采用厂家新版的检测编译库及其配套代码。')
    size = manifest['files']['zero_ota.bin']['bytes']
    angle_note = ('角度映射改为按阵元间距计算理论值：`IS_ANG_ACC_IMPROVE_INTERP=false`，'
                  '128 点角度 FFT；按用户坐标和右到左通道顺序，虚拟阵元有符号间距为 −1.96 mm，'
                  '在当前 76.5 GHz 波形下为约 −0.500146 波长。同步更新八通道阵元位置，幅相系数顺序保持 TX0/TX1 × RX0～RX3。'
                  '厂家角度表仍可读写，但本版测角不使用它。相对于此前正间距定义，角度正负方向会变化，需用已知左右位置确认。'
                  if variant.endswith('_theory') else '角度插值表仍采用对应工程默认值。')
    if variant.endswith('_arraycal'):
        angle_note = ('恢复新版阵列校准流程，`IS_ANG_ACC_IMPROVE_INTERP=true`。八通道编译默认系数为用户提供值；'
                      '128 项角度表编译默认值为按 −1.96 mm、76.5 GHz 计算的理论映射。'
                      '没有有效 Flash 校准时使用这两组默认值；主动校准成功保存后允许覆盖，'
                      '新角度表立即应用，并在重启后从 Flash 加载。新表生成也使用负间距，最后一个角度采样完成后才保存。')
    rows = '\n'.join(f'| TX{i//4}RX{i%4} | {VALUES[2*i]:.6f} | {VALUES[2*i+1]:.6f} |' for i in range(8))
    write(output / 'README_烧录说明.md', f'''# {LABELS[variant]}

生成日期：{manifest['date']}。{algorithm}

## 烧录文件

- 配置：`{cfg_name}`。
- RTOS：`zero_ota.bin`，{size:,} 字节；单槽容量 958,464 字节，剩余 {958464-size:,} 字节。
- SPL：`{SPL}`，24,576 字节，沿用厂家预编译 OTA SPL。
- 将本 cfg 放到正在使用的 `cloner.exe` 旁 `configs/x2100` 中，重新加载配置/重启烧录工具，再选择对应名称。
- cfg 的三个启用项分别写入 SPL、rtos_0、rtos_1；两份 RTOS 均指向本目录的 `zero_ota.bin`，勿混用另一版镜像。
- cfg 内镜像路径为 `{win_output}`；移动镜像目录后须重新指定路径。

## Flash 分区

| 分区 | 起点 | 长度 |
| --- | --- | --- |
| uboot | 0x000000 | 0x6000 |
| rtos_0 | 0x006000 | 0xEA000 |
| rtos_1 | 0x0F0000 | 0xEA000 |
| boot_flag | 0x1DA000 | 0x1000 |
| config | 0x1DB000 | 0x25000 |

`force_erase=1`：烧录会清除旧配置和标定。首次启动使用下表编译默认值；以后使用 `angCalibMat write` 写入有效矩阵，重启后会覆盖默认值。Flash 覆盖功能保留。

## 用户八通道默认补偿

按 TX 优先、每个 TX 内 RX0～RX3 排列；补偿为复数乘法。

| 通道 | 实部 | 虚部 |
| --- | ---: | ---: |
{rows}

波形：2TX/4RX、506 点、128 chirp、26.665 MS/s、16.699 MHz/μs、软件中心 76.5 GHz、chirp 间隔 26 μs、普通帧周期 50.328 ms。默认凯越输出协议，点云/航迹上限为 256/32。

{angle_note} A 版包含厂家校准退出时保存采样尺寸的数组取值修正；B 版同时包含距离、多普勒、测角窗初始化修复。两版安装角逻辑与此前对应版本一致，理论角度版仅供对照实测，不保证消除所有偏移。

已检查最终 ELF/RTOS 中的八通道 float32 值、算法链接来源、RTOS 入口与容量、两槽边界、源文件和输出哈希。编译警告保留在 `build.log`。尚未烧录实测。原 SDK 和原 USBCloner 的 spl/img 文件没有因本次生成而覆盖。
''')


if __name__ == '__main__':
    phase, source, build, output, compiler, variant = sys.argv[1:]
    assert variant in LABELS
    (prepare if phase == 'prepare' else finish)(Path(source), Path(build), Path(output), Path(compiler), variant)
