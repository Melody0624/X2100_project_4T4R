#!/usr/bin/env bash
set -euo pipefail
project=/mnt/d/downloads/X2100_project-main
variant=${1:?Specify A_vendor_new or B_updated_source}
case "$variant" in
    A_vendor_new|A_vendor_new_theory|A_vendor_new_arraycal) source_sdk="$project/MotorCycle_Ingenic/MotorCycle_Ingenic/freertos" ;;
    B_updated_source|B_updated_source_windowfix|B_updated_source_theory|B_updated_source_arraycal) source_sdk="$project/X2100_project-main/firmware/x2100/freertos" ;;
    *) echo "Unknown variant" >&2; exit 1 ;;
esac
build_sdk="/home/melody/Manhattan_Project/freertos_tx01_compare_20261010_$variant"
output="$project/artifacts/2t4r_tx01_8ch_compare_20261010/$variant"
if [[ $variant == *_arraycal ]]; then
    build_sdk="/home/melody/Manhattan_Project/freertos_arraycal_20261012_$variant"
    output="$project/artifacts/array_cal_restore_20261012/$variant"
fi
compiler="$project/MotorCycle_Ingenic/MotorCycle_Ingenic/tools/toolchains/mips-xburst2-newlib430/bin"
mkdir -p "$output"
if [[ -e "$build_sdk" ]]; then
    [[ -f "$build_sdk/.tx01-compare" ]] || exit 1
else
    mkdir -p "$build_sdk"
    touch "$build_sdk/.tx01-compare"
fi
find "$build_sdk" -type f -exec chmod u+w {} +
cp -a "$source_sdk/." "$build_sdk/"
find "$build_sdk" -type f -exec chmod u+w {} +
python3 "$project/tools/package_2t4r_tx01_compare.py" prepare "$source_sdk" "$build_sdk" "$output" "$compiler" "$variant"
cd "$build_sdk"
export PATH="$compiler:$PATH"
chmod +x tools/configparser tools/menuconfig
make clean >"$output/clean.log" 2>&1
make -j8 V=1 >"$output/build.log" 2>&1 || {
    grep -E 'error:|Error|undefined reference|multiple definition' "$output/build.log" | tail -30
    exit 1
}
cp zero.bin "$output/zero_ota.bin"
cp zero.elf "$output/zero.elf"
cp .config.in "$output/build.config"
cp xburst2/soc-x2000/spl/x2100l-spl-sfc-nor-ota-1200M-500M.bin "$output/"
python3 "$project/tools/package_2t4r_tx01_compare.py" finish "$source_sdk" "$build_sdk" "$output" "$compiler" "$variant"
python3 "$project/tools/verify_vendor2t4r_tx01_cal.py" "$output" --elf-only >"$output/verification.txt"
python3 "$project/tools/verify_vendor2t4r_2M_ota.py" "$output" >>"$output/verification.txt"
cat "$output/verification.txt"
echo "Completed: $variant"
