#!/usr/bin/env bash
set -euo pipefail

project_dir=/home/melody/Manhattan_Project/freertos
workspace_dir=/mnt/d/downloads/X2100_project-main
prepared_vendor=${workspace_dir}/migration/adc_replay/vendor.c
prepared_makefile=${workspace_dir}/migration/adc_replay/vendor.Makefile.stage4
prepared_cfg=${workspace_dir}/x2100_kale_sfc_nand_freertos.cfg
output_dir=/mnt/d/X2100_Kale_RTOS
backup_dir=/home/melody/Manhattan_Project/backups/stage7_angle_before_install

cd "${project_dir}"
test -f "${prepared_vendor}"
test -f "${prepared_makefile}"
test -f vendor/bpm_code.h
test -f vendor/ne10_fft_lib/src/NE10_rfft_float32_mxu.c

mkdir -p "${backup_dir}"
if [[ ! -f "${backup_dir}/vendor.c.cfar" ]]; then
    cp -a vendor/vendor.c "${backup_dir}/vendor.c.cfar"
fi
if [[ ! -f "${backup_dir}/vendor.Makefile.cfar" ]]; then
    cp -a vendor/Makefile "${backup_dir}/vendor.Makefile.cfar"
fi

cp "${prepared_vendor}" vendor/vendor.c
cp "${prepared_makefile}" vendor/Makefile

make clean
make -j"$(nproc)"

test -f zero.elf
test -f zero.bin
test -f rtos-with-spl.bin
strings zero.elf | grep '^\[ANGLE\] 5 detections AoA validation passed$' >/dev/null

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
strings zero.elf | grep -E '^\[ANGLE\]|^\[CFAR\]' | head -180
