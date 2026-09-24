#!/usr/bin/env python3
"""Package generated build artifacts, never edit the SDK's source files."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("sdk", type=Path)
p.add_argument("output", type=Path)
p.add_argument("mode", choices=("selftest", "live_guarded"))
a = p.parse_args()
source = Path(__file__).resolve().parent
image = a.sdk / "rtos-with-spl.bin"
assert 0 < image.stat().st_size <= 0x200000, "Image exceeds 2 MiB partition"
a.output.mkdir(parents=True, exist_ok=True)
hashes = {}
for name in ("zero.elf", "zero.bin", "rtos-with-spl.bin"):
    dest = a.output / name
    shutil.copy2(a.sdk / name, dest)
    hashes[name] = {"bytes": dest.stat().st_size,
                    "sha256": hashlib.sha256(dest.read_bytes()).hexdigest()}
# Preserve the known-good board cfg, changing ONLY the image pathname.
cfg = (source / "x2100_kale_sfc_nand_4tx4rx_ddma.cfg").read_text()
output_path = (a.output / "rtos-with-spl.bin").resolve().as_posix()
assert output_path.startswith("/mnt/d/"), "Packaging expects a Windows D: artifact directory"
windows_image = "D:/" + output_path[len("/mnt/d/"):]
cfg = "\n".join("attribute=" + windows_image if line.startswith("attribute=") else line
                for line in cfg.splitlines()) + "\n"
(a.output / ("x2100_kale_bpm_ddma_" + a.mode + ".cfg")).write_text(cfg)
source_hashes = {}
for path in sorted(source.rglob("*")):
    if path.is_file() and "test-results" not in path.parts and path.suffix in (".c", ".h", ".sh", ".py", ".inc", ".json"):
        source_hashes[str(path.relative_to(source))] = hashlib.sha256(path.read_bytes()).hexdigest()
effective_config = json.loads((source / "test-results/effective_config.json").read_text())
manifest = {"mode": a.mode, "rf_validated": False, "array_calibrated": False,
            "board_tested": False, "effective_config": effective_config,
            "partition_bytes": 0x200000, "files": hashes, "source_sha256": source_hashes}
manifest["supplier"] = json.loads((source / "supplier_provenance.json").read_text())
manifest["supplier_staging_options"] = {"mirror": 0, "bpm": 1, "raw_output": 0,
                                        "rf_algorithm_confirmed": False}
manifest["can"] = {"protocol": "20250620-sign-magnitude-strict-subset",
                   "transport_bound": False, "hardware_tested": False,
                   "control_application": False, "ota_flash_write": False}
(a.output / "BUILD_MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
(a.output / "SHA256.txt").write_text("".join(v["sha256"] + "  " + n + "\n" for n, v in hashes.items()))
shutil.copy2(source / "README_BPM_DDMA.md", a.output)
shutil.copy2(source / "radar_4tx4rx_profile.h", a.output)
shutil.copy2(source / "radar_config.h", a.output)
shutil.copy2(source / "OPTIMIZATION_V3.md", a.output)
for name in ("CAN_PROTOCOL.md", "SUPPLIER_INTEGRATION.md", "supplier_provenance.json",
             "supplier_profile_options.h", "supplier_registers.inc"):
    shutil.copy2(source / name, a.output)
for log in (source / "test-results").glob("rf_*.log"):
    shutil.copy2(log, a.output)
shutil.copy2(source / "x2100_kale_radar_4tx4rx_ddma_nand_defconfig",
             a.output / "x2100_kale_bpm_ddma_nand_defconfig")
for log in ("can_protocol.log", "can_protocol_sanitized.log", "resolver.log", "pipeline.log", "stream.log", "resolver_sanitized.log", "pipeline_sanitized.log", "stream_sanitized.log"):
    if (source / "test-results" / log).exists():
        shutil.copy2(source / "test-results" / log, a.output)
print(json.dumps({"mode": a.mode, "output": str(a.output), "files": hashes}, indent=2))
