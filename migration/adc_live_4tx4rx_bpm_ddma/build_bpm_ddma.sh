#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
original_sdk=/home/melody/Manhattan_Project/freertos
build_sdk=/home/melody/Manhattan_Project/freertos_bpm_ddma_20260903
output_root=/mnt/d/downloads/X2100_project-main/artifacts/bpm_ddma_4tx4rx_can_20260910

bash "${source_dir}/test_host.sh"
test -f "${original_sdk}/Makefile"
test -f "${original_sdk}/vendor/ne10_fft_lib/inc/NE10_dsp.h"
# The build must never alter the user's original, dirty SDK checkout.
test "${build_sdk}" != "${original_sdk}"
if [[ ! -e ${build_sdk} ]]; then
    mkdir "${build_sdk}"
    cp -a "${original_sdk}/." "${build_sdk}/"
    cp "${source_dir}/README_BPM_DDMA.md" "${build_sdk}/.bpm-ddma-isolated"
elif [[ ! -f ${build_sdk}/.bpm-ddma-isolated ]]; then
    echo "Refusing an existing directory not marked as this task's SDK copy" >&2
    exit 1
fi
cd "${build_sdk}"
for name in vendor.c complex_abs_f32.c complex_abs_f32.h motorcycle_output.c \
    motorcycle_output.h radar_frontend.c radar_frontend.h radar_4tx4rx_profile.c \
    radar_4tx4rx_profile.h ddma_resolver.c ddma_resolver.h radar_pipeline.h \
    radar_synthetic.c radar_synthetic.h radar_rf_profile.c radar_rf_profile.h bpm_code.h \
    radar_config.h radar_diagnostics.h radar_diagnostics.c \
    supplier_profile_options.h supplier_registers.inc radar_can_protocol.h radar_can_protocol.c; do
    cp "${source_dir}/${name}" "vendor/${name}"
done
cp "${source_dir}/radar_types_live.h" vendor/radar_types.h
cp "${source_dir}/vendor.Makefile.stage4" vendor/Makefile
mkdir -p vendor/cheetah devices/camera/x2000/cheetah package/devices/camera/x2000
cp "${source_dir}/cheetah/cheetah.c" vendor/cheetah/
cp "${source_dir}/cheetah/cheetah.h" vendor/cheetah/
cp "${source_dir}/sensor_cheetah.c" devices/camera/x2000/cheetah/
cp "${source_dir}/radar_types_live.h" devices/camera/x2000/cheetah/radar_types.h
cp "${source_dir}/devices_camera.Makefile" devices/camera/Makefile
cp "${source_dir}/devices_init.c" devices/init.c
cp "${source_dir}/camera.in" package/devices/camera/camera.in
cp "${source_dir}/cheetah.in" package/devices/camera/x2000/cheetah.in
cp "${source_dir}/x2100_kale_radar_4tx4rx_ddma_nand_defconfig" \
    configs/x2100_kale_bpm_ddma_nand_defconfig
# Kconfig input copied from Windows may contain CRLF; this is mechanical formatting.
sed -i 's/\r$//' package/devices/camera/x2000/cheetah.in
make x2100_kale_bpm_ddma_nand_defconfig
mkdir -p "${output_root}"
for mode in selftest live_guarded; do
    if [[ ${mode} == selftest ]]; then input=1; else input=0; fi
    make clean >"${output_root}/${mode}_clean.log" 2>&1
    make -j"$(nproc)" RADAR_BUILD_SELFTEST="${input}" \
        >"${output_root}/${mode}_build.log" 2>&1 || {
            tail -80 "${output_root}/${mode}_build.log" >&2; exit 1;
        }
    strings zero.elf | grep -F '[BPM-DDMA] guarded 4TX4RX pipeline started' >/dev/null
    if [[ ${mode} == selftest ]]; then
        strings zero.elf | grep -F '[SIM] SYNTHETIC ADC ONLY - NOT real radar measurements' >/dev/null
    else
        strings zero.elf | grep -F '[ADC-IN] BLOCKED: verified 4TX RF/array profile missing; no RF writes' >/dev/null
    fi
    python3 "${source_dir}/package_artifacts.py" "${build_sdk}" \
        "${output_root}/${mode}" "${mode}"
done
cp "${source_dir}/test-results/pipeline.log" "${output_root}/host_pipeline.log"
cp "${source_dir}/test-results/resolver.log" "${output_root}/host_resolver.log"
cp "${source_dir}/test-results/stream.log" "${output_root}/host_stream.log"
printf 'ISOLATED_SDK=%s\nOUTPUT=%s\n' "${build_sdk}" "${output_root}"
