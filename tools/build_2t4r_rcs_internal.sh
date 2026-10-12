#!/usr/bin/env bash
# Verify the source B SDK using the established eight-channel/theory defaults.
# This creates a separate build only; it does not create/install a flash cfg.
set -euo pipefail
project=/mnt/d/downloads/X2100_project-main
sdk="$project/X2100_project-main/firmware/x2100/freertos"
build=/home/melody/Manhattan_Project/freertos_rcs_internal_20261012
output="$project/artifacts/2t4r_rcs_internal_20261012"
compiler="$project/MotorCycle_Ingenic/MotorCycle_Ingenic/tools/toolchains/mips-xburst2-newlib430/bin"
mkdir -p "$output/tests"
if [[ -e "$build" ]]; then
    [[ -f "$build/.rcs-internal-build" ]] || exit 1
else
    mkdir -p "$build"
    touch "$build/.rcs-internal-build"
fi
cp -a "$sdk/." "$build/"
find "$build" -type f -exec chmod u+w {} +
python3 "$project/tools/package_2t4r_tx01_compare.py" prepare \
    "$sdk" "$build" "$output" "$compiler" B_updated_source_arraycal
cd "$build"
export PATH="$compiler:$PATH"
chmod +x tools/configparser tools/menuconfig
make clean >"$output/clean.log" 2>&1
make -j8 V=1 >"$output/build.log" 2>&1 || {
    grep -E 'error:|Error|undefined reference|multiple definition' "$output/build.log" | tail -25
    exit 1
}
cp zero.elf "$output/zero.elf"
cp .config.in "$output/build.config"
SOURCE_SDK="$build" TEST_OUTPUT="$output/tests" TEST_THEORY=1 \
    bash "$project/tools/test_vendor2t4r_source_pipeline.sh" >"$output/tests/pipeline_console.log" 2>&1
python3 "$project/tools/verify_vendor2t4r_tx01_cal.py" "$output" --elf-only
echo 'PASS: separate B source build and full source pipeline with RCS'
