#!/usr/bin/env bash
set -euo pipefail

project_dir=/home/melody/Manhattan_Project/freertos
workspace_dir=/mnt/d/downloads/X2100_project-main
source_frame=${workspace_dir}/X2100_project-latest/matlab/Record_20260814_161551_adc.dat
prepared_vendor=${workspace_dir}/migration/adc_replay/vendor.c
prepared_cfg=${workspace_dir}/x2100_kale_sfc_nand_freertos.cfg
output_dir=/mnt/d/X2100_Kale_RTOS
backup_dir=/home/melody/Manhattan_Project/backups/adc_replay_before_install

if [[ ! -d "${project_dir}" ]]; then
    echo "ERROR: project not found: ${project_dir}" >&2
    exit 1
fi

if [[ ! -f "${source_frame}" ]]; then
    echo "ERROR: ADC recording not found: ${source_frame}" >&2
    exit 1
fi

if [[ ! -f "${prepared_vendor}" ]]; then
    echo "ERROR: prepared vendor.c not found: ${prepared_vendor}" >&2
    exit 1
fi

mkdir -p "${backup_dir}"
cp -a "${project_dir}/vendor/vendor.c" "${backup_dir}/vendor.c.heartbeat"

cd "${project_dir}"
mkdir -p vendor/testdata
dd if="${source_frame}" \
   of=vendor/testdata/adc_frame_0001.dat \
   bs=522276 count=1 status=none
cp "${prepared_vendor}" vendor/vendor.c

python3 - <<'PY'
from pathlib import Path
import hashlib

p = Path("vendor/testdata/adc_frame_0001.dat")
data = p.read_bytes()
assert len(data) == 522276, len(data)
payload = data[36:]
assert len(payload) == 522240, len(payload)
print(f"FRAME_BYTES={len(data)}")
print(f"FRAME_SHA256={hashlib.sha256(data).hexdigest()}")
print(f"PAYLOAD_SUM32=0x{sum(payload) & 0xffffffff:08x}")
print("PAYLOAD_FIRST16=" + " ".join(f"{v:02x}" for v in payload[:16]))
PY

make clean
make -j"$(nproc)"

test -f zero.elf
test -f zero.bin
test -f rtos-with-spl.bin

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

echo "BACKUP=${backup_dir}/vendor.c.heartbeat"
echo "FIRMWARE_BYTES=${firmware_bytes}"
echo "PARTITION_BYTES=${partition_bytes}"
sha256sum rtos-with-spl.bin "${output_dir}/rtos-with-spl.bin"
ls -lh zero.elf zero.bin rtos-with-spl.bin
strings zero.elf | grep -E '^\[REPLAY\]|^\[KALE\]' | head -20
