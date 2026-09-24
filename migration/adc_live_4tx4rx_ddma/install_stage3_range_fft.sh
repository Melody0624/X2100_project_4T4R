#!/usr/bin/env bash
set -euo pipefail

project_dir=/home/melody/Manhattan_Project/freertos
workspace_dir=/mnt/d/downloads/X2100_project-main
original_vendor=${workspace_dir}/X2100_project-latest/firmware/x2100/freertos/vendor
prepared_vendor=${workspace_dir}/migration/adc_replay/vendor.c
prepared_makefile=${workspace_dir}/migration/adc_replay/vendor.Makefile.stage3
compat_heap=${workspace_dir}/migration/adc_replay/fft_compat/heap_malloc.h
prepared_cfg=${workspace_dir}/x2100_kale_sfc_nand_freertos.cfg
output_dir=/mnt/d/X2100_Kale_RTOS
backup_dir=/home/melody/Manhattan_Project/backups/stage3_range_fft_before_install

cd "${project_dir}"
test -f "${prepared_vendor}"
test -f "${prepared_makefile}"
test -f "${compat_heap}"
test -f "${original_vendor}/ne10_fft_lib/src/NE10_fft.c"
test -d "${original_vendor}/ne10_fft_lib/inc"

mkdir -p "${backup_dir}"
if [[ ! -f "${backup_dir}/vendor.c.preprocess" ]]; then
    cp -a vendor/vendor.c "${backup_dir}/vendor.c.preprocess"
fi
if [[ ! -f "${backup_dir}/vendor.Makefile.preprocess" ]]; then
    cp -a vendor/Makefile "${backup_dir}/vendor.Makefile.preprocess"
fi

mkdir -p vendor/ne10_fft_lib/inc vendor/ne10_fft_lib/src
cp -a "${original_vendor}/ne10_fft_lib/inc/." vendor/ne10_fft_lib/inc/
cp "${compat_heap}" vendor/ne10_fft_lib/inc/heap_malloc.h
cp "${original_vendor}/ne10_fft_lib/src/NE10_fft.c" \
   vendor/ne10_fft_lib/src/NE10_fft.c
cp "${original_vendor}/ne10_fft_lib/src/NE10_rfft_float32.c" \
   vendor/ne10_fft_lib/src/NE10_rfft_float32.c
cp "${original_vendor}/ne10_fft_lib/src/NE10_rfft_float32_mxu.c" \
   vendor/ne10_fft_lib/src/NE10_rfft_float32_mxu.c
cp "${original_vendor}/ne10_fft_lib/src/NE10_fft_generic_float32.c" \
   vendor/ne10_fft_lib/src/NE10_fft_generic_float32.c
cp "${prepared_vendor}" vendor/vendor.c
cp "${prepared_makefile}" vendor/Makefile

make clean
make -j"$(nproc)"

test -f zero.elf
test -f zero.bin
test -f rtos-with-spl.bin
strings zero.elf | grep '^\[RFFT\] 512-point Range FFT validation passed$' >/dev/null

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
strings zero.elf | grep -E '^\[RFFT\]|^\[PRE\]|^\[ADC\]' | head -80
