#!/usr/bin/env bash
set -euo pipefail

project_dir=/home/melody/Manhattan_Project/freertos
workspace_dir=/mnt/d/downloads/X2100_project-main
opt_dir=${workspace_dir}/migration/adc_live_4tx4rx_ddma_opt
prepared_cfg=${opt_dir}/x2100_kale_sfc_nand_4tx4rx_ddma.cfg
output_dir=${workspace_dir}/artifacts/live_adc_4tx4rx_ddma_opt_20260830
backup_dir=/home/melody/Manhattan_Project/backups/live_adc_4tx4rx_ddma_opt_before_install

cd "${project_dir}"
test -f "${opt_dir}/vendor.c"
test -f "${opt_dir}/radar_frontend.c"
test -f "${opt_dir}/radar_4tx4rx_profile.c"
test -f "${opt_dir}/sensor_cheetah.c"
test -f "${opt_dir}/x2100_kale_radar_4tx4rx_ddma_nand_defconfig"
python3 "${opt_dir}/validate_ddma4.py"
python3 "${opt_dir}/validate_optimization.py"

mkdir -p "${backup_dir}"
cp -a vendor/vendor.c "${backup_dir}/vendor.c"
cp -a vendor/Makefile "${backup_dir}/vendor.Makefile"
cp -a vendor/radar_types.h "${backup_dir}/radar_types.h"
cp -a configs/x2100_kale_nand_defconfig \
    "${backup_dir}/x2100_kale_nand_defconfig"
cp -a devices/camera/Makefile "${backup_dir}/devices_camera.Makefile"
cp -a devices/init.c "${backup_dir}/devices_init.c"
cp -a package/devices/camera/camera.in "${backup_dir}/camera.in"

cp "${opt_dir}/vendor.c" vendor/vendor.c
cp "${opt_dir}/complex_abs_f32.c" vendor/complex_abs_f32.c
cp "${opt_dir}/complex_abs_f32.h" vendor/complex_abs_f32.h
cp "${opt_dir}/motorcycle_output.c" vendor/motorcycle_output.c
cp "${opt_dir}/motorcycle_output.h" vendor/motorcycle_output.h
cp "${opt_dir}/radar_frontend.c" vendor/radar_frontend.c
cp "${opt_dir}/radar_frontend.h" vendor/radar_frontend.h
cp "${opt_dir}/radar_4tx4rx_profile.c" vendor/radar_4tx4rx_profile.c
cp "${opt_dir}/radar_4tx4rx_profile.h" vendor/radar_4tx4rx_profile.h
cp "${opt_dir}/radar_types_live.h" vendor/radar_types.h
mkdir -p vendor/cheetah
cp "${opt_dir}/cheetah/cheetah.c" vendor/cheetah/cheetah.c
cp "${opt_dir}/cheetah/cheetah.h" vendor/cheetah/cheetah.h
cp "${opt_dir}/vendor.Makefile.stage4" vendor/Makefile

mkdir -p devices/camera/x2000/cheetah
cp "${opt_dir}/sensor_cheetah.c" \
    devices/camera/x2000/cheetah/sensor_cheetah.c
cp "${opt_dir}/radar_types_live.h" \
    devices/camera/x2000/cheetah/radar_types.h
cp "${opt_dir}/devices_camera.Makefile" devices/camera/Makefile
cp "${opt_dir}/devices_init.c" devices/init.c
cp "${opt_dir}/camera.in" package/devices/camera/camera.in
mkdir -p package/devices/camera/x2000
cp "${opt_dir}/cheetah.in" package/devices/camera/x2000/cheetah.in
cp "${opt_dir}/x2100_kale_radar_4tx4rx_ddma_nand_defconfig" \
    configs/x2100_kale_radar_4tx4rx_ddma_nand_defconfig

make x2100_kale_radar_4tx4rx_ddma_nand_defconfig
make clean
make -j"$(nproc)"

test -f zero.elf
test -f zero.bin
test -f rtos-with-spl.bin
strings zero.elf | grep \
    '^\[LIVE-OPT\] optimized radar front-end ADC task started$' >/dev/null
strings zero.elf | grep \
    '^\[OPT\] in-place range/Doppler cube saves %u bytes; DDMA map saves %u bytes$' >/dev/null
strings zero.elf | grep \
    '^\[ADC-IN\] initializing Cheetah SPI1 and CSI camera0$' >/dev/null
strings zero.elf | grep \
    '^\[DDMA4\] WARNING: RF registers still use legacy 2TX Cheetah profile$' >/dev/null

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
cp "${prepared_cfg}" \
    "${output_dir}/x2100_kale_sfc_nand_4tx4rx_ddma_opt.cfg"
cp "${opt_dir}/x2100_kale_radar_4tx4rx_ddma_nand_defconfig" "${output_dir}/"
cp "${opt_dir}/radar_4tx4rx_profile.h" "${output_dir}/"
cp "${opt_dir}/README_4TX4RX_DDMA_OPT.md" "${output_dir}/"
cp "${opt_dir}/validate_ddma4.py" "${output_dir}/"
cp "${opt_dir}/validate_optimization.py" "${output_dir}/"

echo "BACKUP_DIR=${backup_dir}"
echo "OUTPUT_DIR=${output_dir}"
echo "FIRMWARE_BYTES=${firmware_bytes}"
echo "PARTITION_BYTES=${partition_bytes}"
sha256sum rtos-with-spl.bin "${output_dir}/rtos-with-spl.bin"
ls -lh zero.elf zero.bin rtos-with-spl.bin
