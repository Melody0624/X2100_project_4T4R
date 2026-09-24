#!/usr/bin/env python3
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("sdk", type=Path)
p.add_argument("output", type=Path)
p.add_argument("mode", choices=("selftest", "adc_capture"))
a = p.parse_args()
source = Path(__file__).resolve().parent
a.output.mkdir(parents=True, exist_ok=True)

files = {}
for name in ("zero.elf", "zero.bin", "rtos-with-spl.bin"):
    src = a.sdk / name
    dst = a.output / name
    shutil.copy2(src, dst)
    files[name] = {
        "bytes": dst.stat().st_size,
        "sha256": hashlib.sha256(dst.read_bytes()).hexdigest(),
    }
assert files["zero.bin"]["bytes"] <= 0x1D4000

spl_name = "x2100l-spl-sfc-nor-ota-1200M-500M.bin"
spl_src = a.sdk / "xburst2" / "soc-x2000" / "spl" / spl_name
spl_dst = a.output / spl_name
shutil.copy2(spl_src, spl_dst)
files[spl_name] = {
    "bytes": spl_dst.stat().st_size,
    "sha256": hashlib.sha256(spl_dst.read_bytes()).hexdigest(),
}

spl_image = spl_dst.resolve().as_posix()
rtos_image = (a.output / "zero.bin").resolve().as_posix()
if spl_image.startswith("/mnt/d/"):
    spl_image = "D:/" + spl_image[len("/mnt/d/"):]
if rtos_image.startswith("/mnt/d/"):
    rtos_image = "D:/" + rtos_image[len("/mnt/d/"):]

cfg = f'''[ddr]
bank8=0
creator_version=2
cs0=1
cs1=0
current_ddr=M54D5121632A_LPDDR2.cfg
current_type="2,LPDDR2"
dw32=0

[debug]
boot_stage_send_length=1
cpu_info_length=5
log=1
read_back_chk=1
stage2_init_timeout=200
str_to_hex=1
transfer_data_chk=1
transfer_size=65536
uart_burn_baudrate=921600
uart_transfer_size=32768

[efuse]
burn_custom_id=0
efuse_en_active=0
efuse_gpio=-1
security_burnkey=0
security_enable=0
security_version=1

[gpio]
config="-1,-1,-1,-1,-1,2,"

[info]
baud_rate=3000000
complete=0
count=0
cpu_and_ddr_freq_limit_index=0
cpufreq=800000000
ddrfreq=400000000
extal=24000000
force_reset=1
power_off=0
sync_time=0
uart_gpio=14

[policy]
policy_count=2

[policy0]
attribute={spl_image}
enabled=1
label=uboot
offset=0x0
ops="12,6,0"
type=0

[policy1]
attribute={rtos_image}
enabled=1
label=rtos_0
offset=0x6000
ops="12,6,0"
type=0

[sfc]
blocksize=32768
boot_quad=1
burn_quad=1
download_params=1
download_params_offset=0x5800
force_erase=0
reserve_space=0
reserve_space_protect=0
sfc_frequency=100000000

[spiPartition]
Partition0="uboot,0x000000,0x6000,PART_RW,MTD_MODE"
Partition1="rtos_0,0x006000,0x1D4000,PART_RW,MTD_MODE"
Partition2="boot_flag,0x1DA000,0x1000,PART_RW,MTD_MODE"
Partition3="config,0x1DB000,0x25000,PART_RW,MTD_MODE"
count=4
'''
cfg_name = f"MT-4T4R_{a.mode}_SFC_NOR_PRESERVE_CONFIG.cfg"
(a.output / cfg_name).write_text(cfg, encoding="utf-8")

source_hashes = {}
for path in sorted(source.rglob("*")):
    if (path.is_file() and "test-results" not in path.parts and path.suffix in (
            ".c", ".h", ".sh", ".py", ".inc")):
        source_hashes[str(path.relative_to(source))] = hashlib.sha256(
            path.read_bytes()).hexdigest()

manifest = {
    "mode": a.mode,
    "target_board": "MT-4T4R-01 REV-A",
    "soc": "X2100L",
    "boot_storage": "XT25F16 2 MiB SFC NOR (user-confirmed for MT-4T4R-01 REV-A on 2026-09-21)",
    "spl": "x2100l-spl-sfc-nor-ota-1200M-500M.bin",
    "firmware_partition": {"offset": 0x6000, "bytes": 0x1D4000},
    "boot_partition": {"offset": 0, "bytes": 0x6000},
    "boot_flag_partition": {"offset": 0x1DA000, "bytes": 0x1000, "written": False},
    "preserved_config_partition": {"offset": 0x1DB000, "bytes": 0x25000},
    "force_erase": False,
    "board_tested": False,
    "files": files,
    "source_sha256": source_hashes,
    "supplier_confirmation": {
        "date": "2026-09-21",
        "provided_by": "user",
        "profile_matches_mt4t4r_rev_a": True,
        "all_four_tx_enabled": True,
        "flash_xt25f16": True,
        "tx_ddma_mapping_known": False,
        "bpm_epoch_known": False,
        "adc_header_semantics_known": False,
        "array_mapping_known": False,
        "complex_calibration_known": False,
    },
    "behavior": {
        "rf_writes": a.mode == "adc_capture",
        "real_adc": a.mode == "adc_capture",
        "raw_frames": 100 if a.mode == "adc_capture" else 0,
        "algorithm_output": a.mode == "selftest",
        "algorithm_input": "synthetic" if a.mode == "selftest" else "disabled",
    },
}
(a.output / "BUILD_MANIFEST.json").write_text(
    json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
(a.output / "SHA256.txt").write_text(
    "".join(v["sha256"] + "  " + n + "\n" for n, v in files.items()),
    encoding="ascii")
for name in ("README_BPM_DDMA.md", "SUPPLIER_INTEGRATION.md", "CAN_PROTOCOL.md",
             "radar_config.h", "supplier_profile_options.h", "supplier_provenance.json"):
    shutil.copy2(source / name, a.output / name)
for name in ("adc_capture_packet.log", "can_protocol.log", "pipeline.log",
             "resolver.log", "stream.log"):
    path = source / "test-results" / name
    if path.exists():
        shutil.copy2(path, a.output / name)
print(json.dumps({"mode": a.mode, "output": str(a.output), "files": files}, indent=2))
