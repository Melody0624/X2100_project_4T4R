#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
original_sdk=/home/melody/Manhattan_Project/freertos
build_sdk=/home/melody/Manhattan_Project/freertos_mt4t4r_supplier_ego_20260924
output_root=/mnt/d/downloads/X2100_project-main/artifacts/mt4t4r_supplier_ego_20260924
tool_prefix=/home/melody/Manhattan_Project/tools/toolchains/mips-xburst2-newlib430/bin/mips-sde-elf

bash "${source_dir}/test_host.sh"
SANITIZE=1 bash "${source_dir}/test_host.sh"
test -f "${original_sdk}/Makefile"
test "${build_sdk}" != "${original_sdk}"
if [[ ! -e ${build_sdk} ]]; then
    mkdir "${build_sdk}"
    cp -a "${original_sdk}/." "${build_sdk}/"
    printf '%s\n' mt4t4r-supplier-tracker-usb-watchdog >"${build_sdk}/.mt4t4r-nor-isolated"
elif [[ ! -f ${build_sdk}/.mt4t4r-nor-isolated ]]; then
    echo "Refusing existing unmarked build directory: ${build_sdk}" >&2
    exit 1
fi

cd "${build_sdk}"
mkdir -p xburst2/soc-x2000/spl
cp -a "/mnt/d/downloads/X2100_project-main/X2100_project-latest/firmware/x2100/freertos/vendor/motor_cycle_demo" vendor/
mkdir -p vendor/mmw_msg_pkt
cp "/mnt/d/downloads/X2100_project-main/X2100_project-latest/firmware/x2100/freertos/vendor/mmw_msg_pkt/mmw_msg_pkt.h" vendor/mmw_msg_pkt/
for name in vendor.c complex_abs_f32.c complex_abs_f32.h motorcycle_output.c \
    motorcycle_output.h adc_capture_packet.c adc_capture_packet.h \
    radar_frontend.c radar_frontend.h radar_4tx4rx_profile.c \
    radar_4tx4rx_profile.h ddma_resolver.c ddma_resolver.h radar_pipeline.h \
    radar_synthetic.c radar_synthetic.h radar_rf_profile.c radar_rf_profile.h \
    bpm_code.h radar_config.h radar_diagnostics.h radar_diagnostics.c \
    radar_tracking.h radar_tracking.c radar_ego_motion.h radar_warning.h radar_warning.c \
    supplier_profile_options.h supplier_registers.inc radar_can_protocol.h \
    radar_can_protocol.c supplier_tracking_adapter.c; do
    cp "${source_dir}/${name}" "vendor/${name}"
done
cp "${source_dir}/motorcycle_output_supplier_usb_watchdog.c" vendor/motorcycle_output.c
cp "${source_dir}/radar_types_live.h" vendor/radar_types.h
cp "${source_dir}/vendor.Makefile.supplier_tracker" vendor/Makefile
cp "${original_sdk}/package/vendor/vendor.mk" package/vendor/vendor.mk
printf '\n' >>package/vendor/vendor.mk
cat "${source_dir}/vendor_package_link.mk" >>package/vendor/vendor.mk
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
"${tool_prefix}-nm" zero.elf | grep -F ' tracking_processing' >/dev/null
"${tool_prefix}-nm" zero.elf | grep -F ' handle_warnings' >/dev/null
"${tool_prefix}-nm" zero.elf | grep -F ' motorcycle_output_publish_full' >/dev/null
"${tool_prefix}-nm" zero.elf | grep -E ' (tracking_processing|handle_warnings|radar_tracking_step|radar_warning_evaluate)$' \
    >"${output_root}/supplier_link_symbols.txt"
python3 "${source_dir}/package_mt4t4r_nor_nonota.py" "${build_sdk}" \
    "${output_root}/${mode}" "${mode}"
rm -f "${output_root}/${mode}/TRACKING_WARNING.md" \
    "${output_root}/${mode}/tracking_warning.log"

cp "${source_dir}/x2100_mt4t4r_product_nor_nonota_115200_fastboot_defconfig" \
    "${output_root}/x2100_mt4t4r_product_nor_defconfig"
cp "${source_dir}/test-results/"*.log "${output_root}/"
cp "${source_dir}/ANTENNA_GEOMETRY.md" "${output_root}/"
cp "${source_dir}/README_FASTBOOT_USB_FIX.md" \
    "${output_root}/README_烧录与串口.md"
cp "${source_dir}/USB_SENDER_V2_TEST.md" "${output_root}/"
cp "${source_dir}/SUPPLIER_TRACKER_EXPERIMENT.md" "${output_root}/"
cp "/mnt/d/downloads/X2100_project-main/tools/monitor_x2100_points.ps1" \
    "${output_root}/"
cat >"${output_root}/README_本版.txt" <<'EOF'
MT-4T4R 原厂航迹/预警库接入实验版：
1. 保留现有4T4R实测ADC/检测/测角链路；点云后调用原厂跟踪库和预警库。
2. 每帧输出点云、航迹、预警，约19.87 Hz；UART2调试串口115200，USB CDC上位机选择460800。
3. 固件位于 live_full_experimental/rtos-with-spl.bin；配套SFC NOR配置文件在同目录。
4. 只通过主机侧算法回归和交叉编译/符号链接检查；未完成本版实板运行、长时间稳定性、已知目标精度与预警方向验证。
5. 从检测点估计自车纵向速度并做径向投影补偿；至少5个可信内点才启用，点不足则保持原始速度。安装角设为180度。
   [EGO] valid=1 表示本帧应用补偿，valid=0 表示本帧未补偿。当前4TX分离/复数幅相校准仍为实验性假设。
6. 之前出现的CPU保留指令异常未定位；本版不作为量产或道路预警程序。
7. USB发送线程改为非阻塞512字节写入；连续约2秒无USB请求完成会打印[HOST-USB]超时，5秒后自动重试。若sent持续不增长，再拔插USB数据线并连接上位机。
EOF
echo "Built MT-4T4R supplier tracking/warning experiment in ${output_root}"
