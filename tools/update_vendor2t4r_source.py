"""Upgrade the tracked old SDK, retaining its source detection pipeline.

The complete previous SDK is copied to artifacts before the replacement is installed.
Run once on Windows; no Cloner spl/img files are touched.
"""
from pathlib import Path
import difflib
import hashlib
import json
import re
import shutil

PROJECT = Path(__file__).resolve().parents[1]
OLD = PROJECT / 'X2100_project-main/firmware/x2100/freertos'
NEW = PROJECT / 'MotorCycle_Ingenic/MotorCycle_Ingenic/freertos'
OUT = PROJECT / 'artifacts/vendor2t4r_source_update_20261010'
STAGE = OUT / 'prepared_sdk'
BACKUP = OUT / 'original_sdk'
CORE = ['detection_processing.c', 'cfar_detection.c', 'cfar_func_v2.c',
        'angle_estimation_v1.c', 'detection_post_processing.c', 'Makefile']
DETECTION = Path('vendor/motor_cycle_demo/src/detection')


def read(path):
    raw = path.read_bytes()
    try:
        text = raw.decode('utf-8-sig')
    except UnicodeDecodeError:
        text = raw.decode('gb18030')
    return text.replace('\r', '')


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode('utf-8'))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    assert OLD.resolve().is_relative_to(PROJECT.resolve())
    assert BACKUP.resolve().is_relative_to((PROJECT / 'artifacts').resolve())
    assert STAGE.resolve().is_relative_to((PROJECT / 'artifacts').resolve())
    assert not BACKUP.exists(), 'Already upgraded; previous SDK backup must be preserved'
    assert not STAGE.exists(), 'Prepared directory exists; inspect it before retrying'
    OUT.mkdir(parents=True, exist_ok=True)
    baseline = {name: read(OLD / DETECTION / name) for name in CORE}
    old_hashes = {name: digest(OLD / DETECTION / name) for name in CORE}
    generated = {'objs', 'symbols', '__pycache__', 'zero.bin', 'zero.elf',
                 'zero.map', 'zero.dump.S', 'rtos-with-spl.bin', '.config'}
    shutil.copytree(NEW, STAGE, ignore=shutil.ignore_patterns(*generated, '*.o', '*.log'))
    for name, original in baseline.items():
        text = original
        text = re.sub(r'->basic_params\.(numTxs|numRxs|minElemSpacing)\b',
                      r'->basicParams.\1[0]', text)
        text = text.replace('->basic_params.', '->basicParams.')
        text = re.sub(r'->wave_params\.(sampling_freq|numChirps|adcLen|central_freq|chirp_period|slope|numEmptyBands|wavelength|rngRes|dopRes|maxUmAmbVlc)\b',
                      r'->waveParams.\1[0]', text)
        text = text.replace('->wave_params.', '->waveParams.')
        text = text.replace('logicIdx_reorder[', 'logicIdx_reorder_sf1[')
        text = text.replace('.azim_fft_in', '.FFTInput').replace('.azim_fft_out', '.FFTRes')
        text = re.sub(r'\bwinRng\b', 'winRng_sf1', text)
        text = re.sub(r'\bwinDop\b', 'winDop_sf1', text)
        text = re.sub(r'\bwinAz\b', 'winAzm_sf1', text)
        if name == 'angle_estimation_v1.c':
            text = text.replace('parabolic_interp(', 'angle_parabolic_interp(')
            text = text.replace('float cur_rng = glbCtx->waveParams.rngRes[0] * rd->rngBinIdx;',
                '/* FFT bin spacing differs from the 506-sample physical resolution. */\n'
                '        float range_bin_m = LIGHT_SPEED * glbCtx->waveParams.sampling_freq[0] /\n'
                '            (2.0f * glbCtx->waveParams.slope[0] * (2 * numRngBins));\n'
                '        float cur_rng = range_bin_m * rd->rngBinIdx;')
            text = text.replace('out->relRDIdx = detIdx;',
                'out->relRDIdx = detIdx;\n'
                '        out->sfID = 0;\n'
                '        out->rngIdx = rd->rngIdx;\n'
                '        out->dopIdx = rd->dopIdx;\n'
                '        out->vlc_cand = out->vlc;\n'
                '        out->vlc_disAmb_fac = 0;\n'
                '        out->vlc_disAmb_conf = VLC_DISAMB_CONF_INIT;')
        if name == 'cfar_detection.c':
            text = text.replace('if ((float)dopBinIdx_ddmaDec * ctx->waveParams.dopRes[0] > (160.0f / 3.6f))',
                                'if (dopBinIdx_ddmaDec >= NFFT_doppler / 2)')
            text = text.replace('ctx->detInfoRD.detObjRD[detCount].rngIdx = rngIdx;',
                                'ctx->detInfoRD.detObjRD[detCount].sfIdx = 0;\n'
                                '                    ctx->detInfoRD.detObjRD[detCount].rngIdx = rngIdx;')
        if name == 'detection_processing.c':
            text = text.replace('#include "memory_pool.h"',
                '#include "memory_pool.h"\n\n'
                '#if NUM_SUBFRAMES != 1 || BOARD_ANTENNA_CONFIG != BOARD_ANTENNA_2T4R || CHEETAH_CONFIG != cheetah_128_512_config\n'
                '#error "Source detection adaptation supports 2T4R 506x128 only"\n'
                '#endif\n'
                'extern volatile uint8_t g_ps_cal_mode;')
            text = text.replace('\t// 参数提取\n\tgnumTxs[pool_index] =',
                '\t/* Calibration mode allocates raw buffers only, not DSP buffers. */\n'
                '\tif (g_ps_cal_mode) return 0;\n'
                '\tif (pool_index < 0 || pool_index >= MAXPOOLNUM) return -1;\n'
                '\t// 参数提取\n\tgnumTxs[pool_index] =', 1)
            call = '\tangle_estimation_v1(ctx, angCalibMat, winAzm_sf1, dopplerFFT_res, maxMetricIndices, gnumRngBins[pool_index], gnumDopBinsPerSubBand[pool_index], gnumVirtualAnts[pool_index], pool_index);'
            assert text.count(call) == 1
            text = text.replace(call, call + '\n\n'
                '\t/* Keep the supplier CRT stage and cache ABI; core detection remains source. */\n'
                '\tunsigned int ranges[NUM_SUBFRAMES] = {gnumRngBins[pool_index]};\n'
                '\tif (ctx->waveParams.isCRT) velocity_disambiguation(ctx, ranges, ctx->detInfo.numDets);\n'
                '\tsave_frame_to_cache(ctx, ctx->detInfo.numDets);')
        write(STAGE / DETECTION / name, text)
        write(OUT / 'detection_changes' / (name + '.patch'), ''.join(difflib.unified_diff(
            original.splitlines(True), text.splitlines(True), fromfile='old/' + name, tofile='updated/' + name)))

    interface = STAGE / 'vendor/motor_cycle_demo/inc/radar_functions.h'
    text = read(interface)
    text = text.replace('unsigned int* maxMetricIndices, int sfIdx, int pool_index);',
                        'unsigned int* maxMetricIndices, int pool_index);', 1)
    text = re.sub(r'void angle_estimation_v1\([^;]+;',
        'void angle_estimation_v1(GlbCtx* ctx, const fft_cpx_f32* angCalibMat, const float* winAzimuth,\n'
        '    const fft_cpx_f32* dopplerFFT_res, unsigned int* maxMetricIndices, int numRangeBins,\n'
        '    int numDoppBinsPerSubBand, int numVirtualAnts, int pool_index);', text, count=1)
    text = text.replace('// CRT速度解模糊',
        'int cfar_func_v2(GlbCtx* ctx, const float* rdmapDb, int numRangeBins, int numDopplerBins,\n'
        '    int* det_flag_mat, float* range_cfar_th_mat, float* doppler_cfar_th_mat,\n'
        '    int* isPeak_flag_mat, int* is_peak_mat_rng, int pool_index);\n\t// CRT速度解模糊')
    write(interface, text)

    for relative in ['vendor/Makefile', 'package/vendor/vendor.mk']:
        path = STAGE / relative
        text = read(path)
        if relative == 'vendor/Makefile':
            text = re.sub(r'# --- DETECTION MODULE CONFIG ---.*?(?=# --- TRACKING MODULE CONFIG ---)',
                '# Source detection is mandatory in this SDK, including RELEASE builds.\n'
                'CONFIG_USE_DETECTION_LIB := n\n'
                'ifeq ($(HAS_DETECTION_SRC),n)\n'
                '$(error Detection sources are required; no library fallback)\n'
                'endif\n\n', text, flags=re.S)
        else:
            text = re.sub(r'ifeq \(\$\(HAS_DETECTION_SRC\), n\).*?(?=ifeq \(\$\(HAS_TRACKING_SRC\), n\))',
                          'CONFIG_USE_DETECTION_LIB := n\n\n', text, flags=re.S)
            text = re.sub(r'\t\$\(if \$\(wildcard \$\(TOPDIR\)vendor/motor_cycle_demo/src/detection/Makefile\).*?Skipping rebuild\."\)\n', '', text, flags=re.S)
            text = re.sub(r'ifeq \(\$\(CONFIG_USE_DETECTION_LIB\), y\).*?endif',
                '# Only the supplier velocity/cache object is retained; no closed core detection.\n'
                'package_lib-y += -L$(TOPDIR)vendor/motor_cycle_demo/mmwlib -lmmw_velocity', text, flags=re.S)
        write(path, text)

    application = STAGE / 'vendor/vendor.c'
    text = read(application).replace(
        'g_ps_cal_saved_chirps = glbCtx[0]->waveParams.numChirps;',
        'g_ps_cal_saved_chirps = glbCtx[0]->waveParams.numChirps[0];').replace(
        'g_ps_cal_saved_adc_len = glbCtx[0]->waveParams.adcLen;',
        'g_ps_cal_saved_adc_len = glbCtx[0]->waveParams.adcLen[0];')
    write(application, text)
    # Build config is explicit for the existing 2 MB OTA board.
    config = STAGE / '.config.in'
    text = read(config).replace('CONFIG_OS_GCC_OPTIMIZATION=-O3', 'CONFIG_OS_GCC_OPTIMIZATION=-Os')
    text = text.replace('spl/x2100l-spl-sfc-nor-1200M-500M.bin',
                        'spl/x2100l-spl-sfc-nor-ota-1200M-500M.bin')
    write(config, text)
    # Leave the input SDK and all existing Cloner image files unchanged.
    manifest = {'old_sdk': str(OLD), 'new_sdk': str(NEW), 'backup_sdk': str(BACKUP),
                'old_detection_sha256': old_hashes, 'board': '2T4R',
                'source_detection': True, 'new_velocity_object': 'velocity_disambiguation.o',
                'hardware_tested': False}
    write(OUT / 'SOURCE_UPDATE_MANIFEST.json', json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    install()


def install():
    # Windows can deny renaming a directory held open by another application.
    # Copy the original tree first, then install only the reviewed staged files.
    assert OLD.resolve().is_relative_to(PROJECT.resolve())
    assert STAGE.resolve().is_relative_to((PROJECT / 'artifacts').resolve())
    assert BACKUP.resolve().is_relative_to((PROJECT / 'artifacts').resolve())
    assert not BACKUP.exists(), 'Refusing to replace the previous SDK backup'
    shutil.copytree(OLD, BACKUP)
    shutil.copytree(STAGE, OLD, dirs_exist_ok=True)
    for name in ['ego_vlc_estimation.h', 'static_zone_management.h',
                 'track_association.h', 'track_update_cv.h', 'vlc_core.h']:
        (OLD / 'vendor/motor_cycle_demo/inc' / name).unlink(missing_ok=True)
    print('Updated:', OLD)
    print('Original SDK backup:', BACKUP)


if __name__ == '__main__':
    import sys
    install() if '--install-prepared' in sys.argv else main()
