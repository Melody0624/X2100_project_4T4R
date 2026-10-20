"""Compile the actual phase-calibration template and parameter preparation."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
sdk = root / 'X2100_project-main/firmware/x2100/freertos'
text = (sdk/'vendor/phase_calib/cheetah_calib.c').read_text()
defines = text[text.index('#define PHASE_CALIB_PROFILE_ADDR'):text.index('#define BYTE_ALIGN')]
template = text[text.index('static const CalibCfg_t s_phase_calib_template'):text.index('static uint32_t phase_calib_checksum')]
work = text[text.index('static void prepare_work_stage'):text.index('static void format_calibration_reg_line')]
out = Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
for mode in (2,4):
    code = '''#include <assert.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#define CHEETAH_H
#define BOARD_ANTENNA_2T4R 0
#define BOARD_ANTENNA_4T4R 2
#include "wave2regs_intf.h"
''' + defines + template + work + '''
int main(void) {
    CalibCfg_t cfg=s_phase_calib_template;
    assert(prepare_phase_calib_topology(&cfg)==0);
    assert(cfg.N_TX==4 && cfg.N_RX==4 && cfg.num_chirp[0]==512);
#if BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_4T4R
    assert(cfg.calib_tx_mask==15 && cfg.num_calib_tx==4 && cfg.num_signal_bands==4);
    assert(cfg.num_bands[0]==8);
    const int unique[]={1,8,4,8};
    for(int tx=0;tx<4;tx++) {
        assert(cfg.NUM_PHASE_PER_TX[tx]==unique[tx]);
        for(int band=0;band<8;band++)
            assert(cfg.nominal_phase_table[tx][band]==(tx*45*band)%360);
    }
    prepare_work_stage(&cfg,64,4,X2100_CALIB_SAMPLE_RATE_HZ);
    assert(cfg.num_chirp[0]==64 && cfg.ddma_num[0]==8);
    assert(fabs(cfg.slope[0]/19.53125E12-1)<1e-12);
    prepare_work_stage(&cfg,128,1,X2100_WORK_SAMPLE_RATE_HZ);
    assert(fabs(cfg.slope[0]/16.69921875E12-1)<1e-12);
#else
    assert(cfg.calib_tx_mask==3 && cfg.num_calib_tx==2 && cfg.num_signal_bands==2);
    assert(cfg.num_bands[0]==4 && cfg.NUM_PHASE_PER_TX[1]==4);
    prepare_work_stage(&cfg,32,4,X2100_CALIB_SAMPLE_RATE_HZ);
    assert(cfg.num_chirp[0]==32 && cfg.ddma_num[0]==4);
    assert(fabs(cfg.slope[0]/19.53125E12-1)<1e-12);
#endif
    puts("PASS: actual phase-calibration topology and stage parameters");
}
'''
    c=out/f'phase_{mode}tx.c';c.write_text(code)
    binary=out/f'phase_{mode}tx'
    subprocess.run(['gcc','-std=gnu99','-Wall','-Wextra','-Werror',
                    f'-DBOARD_ANTENNA_CONFIG={2 if mode==4 else 0}',
                    '-I'+str(sdk/'vendor/phase_calib'),'-I'+str(sdk/'vendor/cheetah'),
                    str(c),'-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
