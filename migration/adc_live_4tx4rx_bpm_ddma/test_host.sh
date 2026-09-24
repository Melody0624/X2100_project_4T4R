#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
test_dir=${source_dir}/test-results
mkdir -p "${test_dir}"
cd "${source_dir}"
flags=(-std=c11 -D_POSIX_C_SOURCE=200809L -O2 -g -I. -Itests/include)
gcc -std=c11 -Wall -Wextra -Werror -I. tests/config_dump.c -o "${test_dir}/config_dump"
"${test_dir}/config_dump" > "${test_dir}/effective_config.json"
suffix=
if [[ ${SANITIZE:-0} == 1 ]]; then
    # A fixed executable mapping avoids intermittent ASan/PIE shadow-map
    # startup faults on this WSL host. All sanitizer instrumentation stays on.
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie)
    suffix=_sanitized
    # Instrumented violations still abort. Let native SIGSEGV fail the test
    # directly rather than recurse in this WSL host's ASan signal reporter.
    export ASAN_OPTIONS=${ASAN_OPTIONS:-abort_on_error=1:handle_segv=0}
fi
gcc "${flags[@]}" -Wall -Wextra -Werror tests/test_can_protocol.c radar_can_protocol.c \
    -o "${test_dir}/test_can_protocol${suffix}"
"${test_dir}/test_can_protocol${suffix}" | tee "${test_dir}/can_protocol${suffix}.log"

gcc "${flags[@]}" -Wall -Wextra -Werror tests/test_adc_capture_packet.c \
    adc_capture_packet.c -o "${test_dir}/test_adc_capture_packet${suffix}"
"${test_dir}/test_adc_capture_packet${suffix}" | \
    tee "${test_dir}/adc_capture_packet${suffix}.log"

for mirror in 0 1; do
    for bpm in 0 1; do
        for raw in 0 1; do
            gcc "${flags[@]}" -Wall -Wextra -Werror \
                -DSUPPLIER_IS_MIRROR="$mirror" -DSUPPLIER_USE_BPM="$bpm" \
                -DSUPPLIER_UART_ADC_SEND="$raw" tests/test_rf_profile.c radar_rf_profile.c \
                -o "${test_dir}/test_rf_${mirror}${bpm}${raw}${suffix}"
            "${test_dir}/test_rf_${mirror}${bpm}${raw}${suffix}" \
                > "${test_dir}/rf_${mirror}${bpm}${raw}${suffix}.log"
        done
    done
done

gcc "${flags[@]}" -Wall -Wextra -Werror tests/test_resolver.c ddma_resolver.c \
    -lm -o "${test_dir}/test_resolver${suffix}"
timeout --kill-after=2s 45s "${test_dir}/test_resolver${suffix}" | tee "${test_dir}/resolver${suffix}.log"

gcc "${flags[@]}" -Wall -Wextra -Werror tests/test_geometry.c radar_4tx4rx_profile.c \
    -lm -o "${test_dir}/test_geometry${suffix}"
"${test_dir}/test_geometry${suffix}" | tee "${test_dir}/geometry${suffix}.log"
gcc "${flags[@]}" -Wall -Wextra -Werror tests/test_ego_motion.c \
    -lm -o "${test_dir}/test_ego_motion${suffix}"
"${test_dir}/test_ego_motion${suffix}" | tee "${test_dir}/ego_motion${suffix}.log"
gcc "${flags[@]}" tests/test_pipeline.c tests/host_backend.c vendor.c radar_can_protocol.c \
    ddma_resolver.c radar_4tx4rx_profile.c radar_synthetic.c radar_rf_profile.c radar_diagnostics.c \
    radar_tracking.c radar_warning.c complex_abs_f32.c -lm -o "${test_dir}/test_pipeline${suffix}"
timeout --kill-after=2s 45s "${test_dir}/test_pipeline${suffix}" | tee "${test_dir}/pipeline${suffix}.log"

gcc "${flags[@]}" tests/test_stream.c tests/host_backend.c vendor.c radar_can_protocol.c \
    ddma_resolver.c radar_4tx4rx_profile.c radar_synthetic.c radar_rf_profile.c \
    radar_diagnostics.c radar_tracking.c radar_warning.c complex_abs_f32.c -lm -o "${test_dir}/test_stream${suffix}"
timeout --kill-after=2s 60s "${test_dir}/test_stream${suffix}" | tee "${test_dir}/stream${suffix}.log"

gcc "${flags[@]}" -Wall -Wextra -Werror tests/test_tracking_warning.c \
    radar_tracking.c radar_warning.c -lm -o "${test_dir}/test_tracking_warning${suffix}"
"${test_dir}/test_tracking_warning${suffix}" | \
    tee "${test_dir}/tracking_warning${suffix}.log"
