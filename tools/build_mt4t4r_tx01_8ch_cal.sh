#!/usr/bin/env bash
set -euo pipefail

root=/mnt/d/downloads/X2100_project-main
source_sdk="$root/X2100_project-latest/firmware/x2100/freertos"
calib_sdk="$root/X2100_project-main/firmware/x2100/freertos"
build_sdk=/home/melody/Manhattan_Project/freertos_mt4t4r_tx01_cal_20260929
output="$root/artifacts/mt4t4r_tx01_8ch_cal_20260929"
mkdir -p "$output"

if [[ ! -e "$build_sdk" ]]; then
    mkdir -p "$build_sdk"
    cp -a "$source_sdk/." "$build_sdk/"
    touch "$build_sdk/.mt4t4r-tx01-cal-isolated"
fi
[[ -f "$build_sdk/.mt4t4r-tx01-cal-isolated" ]] || {
    echo "Refusing to overwrite an unrelated build directory" >&2
    exit 1
}

# The Windows SDK snapshot has CRLF shell scripts and Kconfig files.  Fix only
# text files in the isolated Linux copy; leave the user's source untouched.
python3 - "$build_sdk" <<'PY'
from pathlib import Path
import sys

suffixes = {".sh", ".c", ".h", ".in", ".mk", ".py", ".S", ".defconfig"}
for path in Path(sys.argv[1]).rglob("*"):
    if not path.is_file() or path.is_symlink():
        continue
    if path.suffix not in suffixes and path.name != "Makefile" and not path.name.endswith("_defconfig"):
        continue
    data = path.read_bytes()
    if b"\0" not in data[:4096] and b"\r\n" in data:
        path.write_bytes(data.replace(b"\r\n", b"\n"))
PY

for file in inc/board_calib_config.h src/board_calib_config.c; do
    cp "$calib_sdk/vendor/motor_cycle_demo/$file" \
       "$build_sdk/vendor/motor_cycle_demo/$file"
done
cd "$build_sdk"
make x2100_Cheetah_RadarEye_nor_single_defconfig >"$output/config.log" 2>&1
make clean >"$output/clean.log" 2>&1
make -j8 >"$output/build.log" 2>&1 || {
    tail -80 "$output/build.log" >&2
    exit 1
}

test -s rtos-with-spl.bin
bytes=$(stat -c %s rtos-with-spl.bin)
(( bytes <= 0x1DB000 )) || { echo "Image exceeds uboot partition" >&2; exit 1; }
cp rtos-with-spl.bin zero.bin zero.elf "$output/"
cp .config.in "$output/x2100_Cheetah_RadarEye_nor_single.config"
python3 - "$root/X2100_project-latest/shaolu/x2100_radareye_2m_single.cfg" "$output" <<'PY'
from pathlib import Path
import sys

source, output = Path(sys.argv[1]), Path(sys.argv[2])
cfg = source.read_text(encoding="utf-8")
lines = cfg.splitlines()
for i, line in enumerate(lines):
    if line.startswith("attribute="):
        lines[i] = "attribute=" + (output / "rtos-with-spl.bin").as_posix().replace("/mnt/d/", "D:/", 1)
        break
else:
    raise RuntimeError("No firmware attribute in burn config")
(output / "x2100_radareye_2m_single_8ch_cal.cfg").write_text("\n".join(lines) + "\n", encoding="utf-8")
PY
sha256sum "$output/rtos-with-spl.bin" >"$output/SHA256.txt"
echo "Built $output/rtos-with-spl.bin ($bytes bytes)"
