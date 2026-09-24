#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
original_sdk=/home/melody/Manhattan_Project/freertos
build_sdk=/home/melody/Manhattan_Project/freertos_mt4t4r_nor_fastboot_usbfix_everyframe_20260923
output_root=/mnt/d/downloads/X2100_project-main/artifacts/mt4t4r_nor_fastboot_usbfix_everyframe_20260923
tool_prefix=/home/melody/Manhattan_Project/tools/toolchains/mips-xburst2-newlib430/bin/mips-sde-elf

bash "${source_dir}/test_host.sh"
SANITIZE=1 bash "${source_dir}/test_host.sh"
test -f "${original_sdk}/Makefile"
test "${build_sdk}" != "${original_sdk}"
if [[ ! -e ${build_sdk} ]]; then
    mkdir "${build_sdk}"
    cp -a "${original_sdk}/." "${build_sdk}/"
    printf '%s\n' mt4t4r-nor-fastboot-usbfix-everyframe >"${build_sdk}/.mt4t4r-nor-isolated"
elif [[ ! -f ${build_sdk}/.mt4t4r-nor-isolated ]]; then
    echo "Refusing existing unmarked build directory: ${build_sdk}" >&2
    exit 1
fi

cd "${build_sdk}"
mkdir -p xburst2/soc-x2000/spl
for name in vendor.c complex_abs_f32.c complex_abs_f32.h motorcycle_output.c \
    motorcycle_output.h adc_capture_packet.c adc_capture_packet.h \
    radar_frontend.c radar_frontend.h radar_4tx4rx_profile.c \
    radar_4tx4rx_profile.h ddma_resolver.c ddma_resolver.h radar_pipeline.h \
    radar_synthetic.c radar_synthetic.h radar_rf_profile.c radar_rf_profile.h \
    bpm_code.h radar_config.h radar_diagnostics.h radar_diagnostics.c \
    radar_tracking.h radar_tracking.c radar_warning.h radar_warning.c \
    supplier_profile_options.h supplier_registers.inc radar_can_protocol.h \
    radar_can_protocol.c; do
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
cp "${source_dir}/x2100_mt4t4r_product_nor_nonota_115200_fastboot_defconfig" \
    configs/x2100_mt4t4r_product_nor_defconfig
sed -i 's/\r$//' package/devices/camera/x2000/cheetah.in \
    configs/x2100_mt4t4r_product_nor_defconfig
make x2100_mt4t4r_product_nor_defconfig

mkdir -p "${output_root}"
mode=live_full_experimental
make clean >"${output_root}/${mode}_clean.log" 2>&1
make -j"$(nproc)" RADAR_BUILD_SELFTEST=0 RADAR_BUILD_CAPTURE_ONLY=0 \
    RADAR_BUILD_EXPERIMENTAL_LIVE=1 \
    >"${output_root}/${mode}_build.log" 2>&1 || {
        tail -100 "${output_root}/${mode}_build.log" >&2
        exit 1
    }
"${tool_prefix}-nm" zero.elf | grep -F ' radar_tracking_step' >/dev/null
"${tool_prefix}-nm" zero.elf | grep -F ' radar_warning_evaluate' >/dev/null
"${tool_prefix}-nm" zero.elf | grep -F ' motorcycle_output_publish_full' >/dev/null
python3 "${source_dir}/package_mt4t4r_nor_nonota.py" "${build_sdk}" \
    "${output_root}/${mode}" "${mode}"

cp "${source_dir}/x2100_mt4t4r_product_nor_nonota_115200_fastboot_defconfig" \
    "${output_root}/x2100_mt4t4r_product_nor_defconfig"
cp "${source_dir}/test-results/"*.log "${output_root}/"
cp "${source_dir}/TRACKING_WARNING.md" "${output_root}/"
cp "${source_dir}/README_FASTBOOT_USB_FIX.md" \
    "${output_root}/README_烧录与串口.md"
cp "${source_dir}/USB_SENDER_V2_TEST.md" "${output_root}/"
cp "/mnt/d/downloads/X2100_project-main/tools/monitor_x2100_points.ps1" \
    "${output_root}/"
cat >"${output_root}/USB发送修复说明.txt" <<'EOF'
USB发送修复 every-frame：
1. 保留快速启动：关闭未使用的 MMC2 和以太网初始化。
2. UART2 调试串口为 115200；USB CDC 上位机选择 460800。
3. 点云/航迹/预警恢复为每帧输出，约 19.87 Hz；算法也逐帧运行。
4. USB 点云发送严格复现原 2T4R 固件：每次最多 512 字节，并在独立发送线程中等待 USB IN 请求完成/归还。
5. 该修改用于消除一次排入多个请求后出现的 -116 超时和请求池耗尽。
EOF
echo "Built MT-4T4R USB sender every-frame candidate in ${output_root}"
