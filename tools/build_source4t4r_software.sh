#!/usr/bin/env bash
# Build the source-based 4T4R SDK with its tracked controlled-HS configuration.
set -euo pipefail
project=$(cd "$(dirname "$0")/.." && pwd)
source_sdk="$project/X2100_project-main/firmware/x2100/freertos"
baseline=${BASELINE_PROJECT:-/mnt/d/downloads/X2100_project-main}
compiler=${MIPS_TOOLCHAIN_PATH:-$baseline/MotorCycle_Ingenic/MotorCycle_Ingenic/tools/toolchains/mips-xburst2-newlib430/bin}
build=${BUILD_DIR:-/home/melody/Manhattan_Project/freertos_source4t4r_software_20261012}
output=${BUILD_OUTPUT:-$project/artifacts/source4t4r_adaptation_20261012/mips}
mkdir -p "$output"
if [[ -e $build ]]; then
    [[ -f $build/.source4t4r-software ]] || exit 1
    find "$build" -type f -exec chmod u+w {} +
else
    mkdir -p "$build"
    touch "$build/.source4t4r-software"
fi
cp -a "$source_sdk/." "$build/"
find "$build" -type f -exec chmod u+w {} +
python3 - "$project" "$source_sdk" "$build" "$output" "$compiler" "$baseline" <<'PY'
from pathlib import Path
import hashlib,json,shutil,sys
project,source,build,out,compiler,baseline=map(Path,sys.argv[1:])
sys.path.insert(0,str(project/'tools'))
from package_2t4r_tx01_compare import inputs,SPL
snapshot={}
for name,path in inputs(source).items():
    raw=path.read_bytes();snapshot[name]=hashlib.sha256(raw).hexdigest()
    if path.suffix!='.a': (build/name).write_bytes(raw.replace(b'\r',b''))
(out/'source_snapshot.json').write_text(json.dumps(snapshot,indent=2)+'\n')
spl_dir=build/'xburst2/soc-x2000/spl';spl_dir.mkdir(parents=True,exist_ok=True)
spl_source=source/'xburst2/soc-x2000/spl'/SPL
shutil.copyfile(spl_source,spl_dir/SPL)
cfg=build/'.config.in'
cfg_source=source/'configs/x2100_4t4r_controlled_hs_defconfig'
if not cfg_source.exists(): cfg_source=baseline/'X2100_project-main/firmware/x2100/freertos/.config.in'
cfg.write_bytes(cfg_source.read_bytes().replace(b'\r',b''))
(out/'BASELINE_BUILD_INPUTS.json').write_text(json.dumps({
    'config_source':str(cfg_source),'config_sha256':hashlib.sha256(cfg_source.read_bytes()).hexdigest(),
    'spl_source':str(spl_source),'spl_sha256':hashlib.sha256(spl_source.read_bytes()).hexdigest()},indent=2)+'\n')
lines=cfg.read_text().splitlines()
settings={'RTOS_TOOLCHAIN_PATH':str(compiler),'CONFIG_OS_GCC_OPTIMIZATION':'-Os','CONFIG_SPL_BIN_FILE':'xburst2/soc-x2000/spl/'+SPL}
for k,v in settings.items():
    hits=[i for i,line in enumerate(lines) if line.startswith(k+'=')];assert len(hits)==1,k
    lines[hits[0]]=k+'='+v
cfg.write_text('\n'.join(lines)+'\n')
PY
cd "$build"
export PATH="$compiler:$PATH"
chmod +x tools/configparser tools/menuconfig
make clean >"$output/clean.log" 2>&1
make -j8 V=1 >"$output/build.log" 2>&1 || {
    grep -E 'error:|Error|undefined reference|multiple definition' "$output/build.log" | tail -25
    exit 1
}
cp zero.elf "$output/zero.elf"
python3 - "$source_sdk" "$output" <<'PY'
from pathlib import Path
import re,shutil,sys
source,out=map(Path,sys.argv[1:])
header=(source/'vendor/motor_cycle_demo/inc/radar_types.h').read_text()
ready=int(re.search(r'^#define SOURCE_4T4R_RF_READY ([01])$',header,re.M).group(1))
shutil.copyfile('zero.bin',out/('zero_ota.bin' if ready else 'zero_software_only.bin'))
PY
cp .config.in "$output/build.config"
python3 "$project/tools/verify_source4t4r_software.py" "$source_sdk" "$build" "$output" "$baseline"
