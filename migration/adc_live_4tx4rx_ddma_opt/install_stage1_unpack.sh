#!/usr/bin/env bash
set -euo pipefail

project_dir=/home/melody/Manhattan_Project/freertos
workspace_dir=/mnt/d/downloads/X2100_project-main
prepared_vendor=${workspace_dir}/migration/adc_replay/vendor.c
prepared_cfg=${workspace_dir}/x2100_kale_sfc_nand_freertos.cfg
output_dir=/mnt/d/X2100_Kale_RTOS
backup_dir=/home/melody/Manhattan_Project/backups/adc_unpack_before_install

cd "${project_dir}"
test -f vendor/testdata/adc_frame_0001.dat
test -f "${prepared_vendor}"

mkdir -p "${backup_dir}"
if [[ ! -f "${backup_dir}/vendor.c.replay-header-only" ]]; then
    cp -a vendor/vendor.c "${backup_dir}/vendor.c.replay-header-only"
fi
cp "${prepared_vendor}" vendor/vendor.c

make clean
make -j"$(nproc)"

test -f zero.elf
test -f zero.bin
test -f rtos-with-spl.bin
strings zero.elf | grep '^\[ADC\] unpack validation passed$' >/dev/null

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

echo "BACKUP=${backup_dir}/vendor.c.replay-header-only"
echo "FIRMWARE_BYTES=${firmware_bytes}"
echo "PARTITION_BYTES=${partition_bytes}"
sha256sum rtos-with-spl.bin "${output_dir}/rtos-with-spl.bin"
ls -lh zero.elf zero.bin rtos-with-spl.bin
strings zero.elf | grep -E '^\[ADC\]|^\[REPLAY\]' | head -40
