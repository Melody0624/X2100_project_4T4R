"""Compile the actual USB CLI matrix handler with mocked Flash/USB boundaries."""
from pathlib import Path
import subprocess,sys

root=Path(__file__).resolve().parents[1]
source=root/'X2100_project-main/firmware/x2100/freertos'
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
cli=source/'vendor/uart_cli/cli.c'
if '--baseline' in sys.argv:
    cli=Path('/mnt/d/downloads/X2100_project-main/X2100_project-main/firmware/x2100/freertos/vendor/uart_cli/cli.c')
text=cli.read_text();start=text.index('static int32_t MagAngCalibMat(')
brace=text.index('{',start);depth=1;end=brace+1
while depth:
    depth+=(text[end]=='{')-(text[end]=='}');end+=1
handler=text[start:end]
prelude=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "radar_types.h"
#include "config_manager.h"
static float stored[2*NUM_VX_SF1];
static int writes,rows;
int param_get(ParamID id,void *p,uint32_t n) {
    assert(id==PARAM_ANG_CALIB_MAT && n==sizeof(stored));memcpy(p,stored,n);return 0;
}
int param_set(ParamID id,const void *p,uint32_t n) {
    assert(id==PARAM_ANG_CALIB_MAT && n==sizeof(stored));memcpy(stored,p,n);writes++;return 0;
}
int usb_serial_send(char *p,int n) { if(strstr(p,"  { "))rows++;return n; }
'''
main=r'''
int main(void) {
    char values[2*NUM_VX_SF1][32];char *argv[2*NUM_VX_SF1+3];
    argv[0]="angCalibMat";argv[1]="write";
    for(int i=0;i<2*NUM_VX_SF1;i++){sprintf(values[i],"%.2f",i+1.25f);argv[i+2]=values[i];}
    assert(MagAngCalibMat(2+2*NUM_VX_SF1,argv)==0 && writes==1);
    for(int i=0;i<2*NUM_VX_SF1;i++)assert(stored[i]==i+1.25f);
    assert(MagAngCalibMat(2+2*NUM_VX_SF1-1,argv)==-1 && writes==1);
    argv[2+2*NUM_VX_SF1]="99";
    assert(MagAngCalibMat(2+2*NUM_VX_SF1+1,argv)==-1 && writes==1);
    argv[1]="read";assert(MagAngCalibMat(2,argv)==0 && rows==NUM_VX_SF1);
    puts("PASS: actual USB CLI writes/reads all matrix channels and rejects incomplete/excess arguments");
}
'''
c=out/'cli_matrix.c';c.write_text(prelude+handler+main)
flags=['-DBOARD_ANTENNA_CONFIG=BOARD_ANTENNA_2T4R'] if '--two' in sys.argv else []
cmd=['gcc','-std=gnu99','-g','-fno-pie','-no-pie','-fsanitize=address,undefined',*flags,
     '-I'+str(source/'vendor/motor_cycle_demo/inc'),'-I'+str(source/'vendor/ne10_fft_lib/inc'),
     '-I'+str(source/'vendor'),str(c),'-o',str(out/'cli_matrix')]
subprocess.run(cmd,check=True,timeout=30)
result=subprocess.run([str(out/'cli_matrix')],capture_output=True,text=True,timeout=10)
(out/'cli_matrix.log').write_text(result.stdout+result.stderr)
print((result.stdout+result.stderr)[:1500])
raise SystemExit(result.returncode!=0)
