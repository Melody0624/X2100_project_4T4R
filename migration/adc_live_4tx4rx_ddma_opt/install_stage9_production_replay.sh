#!/usr/bin/env bash
set -euo pipefail

project_dir=/home/melody/Manhattan_Project/freertos
workspace_dir=/mnt/d/downloads/X2100_project-main
prepared_vendor=${workspace_dir}/migration/adc_replay/vendor.c
prepared_complex_abs_c=${workspace_dir}/migration/adc_replay/complex_abs_f32.c
prepared_complex_abs_h=${workspace_dir}/migration/adc_replay/complex_abs_f32.h
prepared_motorcycle_output_c=${workspace_dir}/migration/adc_replay/motorcycle_output.c
prepared_motorcycle_output_h=${workspace_dir}/migration/adc_replay/motorcycle_output.h
prepared_makefile=${workspace_dir}/migration/adc_replay/vendor.Makefile.stage4
prepared_cfg=${workspace_dir}/x2100_kale_sfc_nand_freertos.cfg
output_dir=/mnt/d/X2100_Kale_RTOS
backup_dir=/home/melody/Manhattan_Project/backups/stage9_production_replay_before_install

cd "${project_dir}"
test -f "${prepared_vendor}"
test -f "${prepared_complex_abs_c}"
test -f "${prepared_complex_abs_h}"
test -f "${prepared_motorcycle_output_c}"
test -f "${prepared_motorcycle_output_h}"
test -f "${prepared_makefile}"
test -f vendor/bpm_code.h
test -f vendor/ne10_fft_lib/src/NE10_rfft_float32_mxu.c

mkdir -p "${backup_dir}"
if [[ ! -f "${backup_dir}/vendor.c.stage8" ]]; then
    cp -a vendor/vendor.c "${backup_dir}/vendor.c.stage8"
fi
if [[ ! -f "${backup_dir}/vendor.Makefile.stage8" ]]; then
    cp -a vendor/Makefile "${backup_dir}/vendor.Makefile.stage8"
fi

cp "${prepared_vendor}" vendor/vendor.c
cp "${prepared_complex_abs_c}" vendor/complex_abs_f32.c
cp "${prepared_complex_abs_h}" vendor/complex_abs_f32.h
cp "${prepared_motorcycle_output_c}" vendor/motorcycle_output.c
cp "${prepared_motorcycle_output_h}" vendor/motorcycle_output.h
rm -f vendor/point_usb_output.c vendor/point_usb_output.h
cp "${prepared_makefile}" vendor/Makefile

make clean
make -j"$(nproc)"

test -f zero.elf
test -f zero.bin
test -f rtos-with-spl.bin
strings zero.elf | grep '^\[PROD\] continuous replay task started$' >/dev/null
strings zero.elf | grep '^\[PERF\] frames=' >/dev/null
strings zero.elf | grep '^\[HOST\] MotorCycle protocol init=' >/dev/null

firmware_bytes=$(stat -c '%s' rtos-with-spl.bin)
partition_bytes=$((0x200000))
if (( firmware_bytes > partition_bytes )); then
    echo "ERROR: firmware ${firmware_bytes} exceeds partition ${partition_bytes}" >&2
    exit 1
fi

mkdir -p "${output_dir}"
cp rtos-with-spl.bin "${output_dir}/rtos-with-spl.bin"
cp zero.bin "${output_dir}/zero.bin"
cp zero.elf "${output_dir}/zero.elf"
cp "${prepared_cfg}" "${output_dir}/x2100_kale_sfc_nand_freertos.cfg"

echo "BACKUP_DIR=${backup_dir}"
echo "FIRMWARE_BYTES=${firmware_bytes}"
echo "PARTITION_BYTES=${partition_bytes}"
sha256sum rtos-with-spl.bin "${output_dir}/rtos-with-spl.bin"
ls -lh zero.elf zero.bin rtos-with-spl.bin
strings zero.elf | grep -E '^\[PROD\]|^\[PERF\]' | head -40
