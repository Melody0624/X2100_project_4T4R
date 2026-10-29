"""Validate the real RTOS image, OTA SPL, ELF and 2 MiB A/B burn package."""
import argparse
import configparser
import hashlib
import json
import os
import struct
from pathlib import Path

from verify_vendor2t4r_tx01_cal import symbol_bytes

SLOT_SIZE = 0xEA000
SPL_NAME = "x2100l-spl-sfc-nor-ota-1200M-500M.bin"
PARTITIONS = (
    "uboot,0x000000,0x6000,PART_RW,MTD_MODE",
    "rtos_0,0x6000,0xEA000,PART_RW,MTD_MODE",
    "rtos_1,0xF0000,0xEA000,PART_RW,MTD_MODE",
    "boot_flag,0x1DA000,0x1000,PART_RW,MTD_MODE",
    "config,0x1DB000,0x25000,PART_RW,MTD_MODE",
)


def check_image(path):
    data = path.read_bytes()
    assert 36 <= len(data) <= SLOT_SIZE, (
        f"RTOS image {len(data)} bytes exceeds slot {SLOT_SIZE} bytes"
    )
    code0, code1, tag, version, start, end, heap_start, heap_end, mapped = (
        struct.unpack_from("<9I", data)
    )
    assert tag == 0x534F5452, "RTOS header must be at offset zero (no SPL prefix)"
    assert code0 == 0x10000008 and code1 == 0, "Unexpected MIPS entry branch"
    assert start == 0x80100000 and end - start == len(data), "RTOS image bounds mismatch"
    assert end <= heap_start < heap_end <= 0x84000000, "Invalid heap bounds"
    print(f"PASS RTOS header and slot: {len(data)} bytes; free {SLOT_SIZE-len(data)} bytes")
    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    parser.add_argument("--image-only", action="store_true")
    args = parser.parse_args()
    if args.image_only:
        check_image(args.path)
        return
    out = args.path.resolve()
    image = check_image(out / "zero_ota.bin")
    spl = (out / SPL_NAME).read_bytes()
    assert len(spl) == 0x6000, "SPL must fit its separate 24 KiB partition"
    manifest = json.loads((out / "BUILD_MANIFEST.json").read_text(encoding="utf-8"))
    source_text = manifest["source_sdk"]
    if os.name == "nt" and source_text.startswith("/mnt/d/"):
        source_text = source_text.replace("/mnt/d/", "D:/", 1)
    source = Path(source_text)
    assert spl == (source / "xburst2/soc-x2000/spl" / SPL_NAME).read_bytes()
    assert spl != (source / "xburst2/soc-x2000/spl/x2100l-spl-sfc-nor-1200M-500M.bin").read_bytes()
    elf = (out / "zero.elf").read_bytes()
    for name in (
        "vendor_init", "detection_processing", "detection_processing_v2",
        "tracking_processing", "handle_warnings", "compute_ego_motion",
        "ota_trigger_ab_init", "rtos_ota_init", "phase_calib_task",
        ("mappingGen_processing_with_spacing" if manifest.get("angle_mapping") in ("theory_defaults_with_saved_calibration", "factory_defaults_with_saved_calibration") else "mappingGen_processing"), "gadget_serial_init", "mmw_packetize_results",
    ):
        assert symbol_bytes(elf, name), f"Missing linked function: {name}"
    assert symbol_bytes(elf, "angCalibMat").hex() == manifest["linked_calibration_hex"]
    print("PASS linked detection, tracking, warning, ego, calibration, USB and OTA functions")
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.read(out / manifest.get("cfg_file", "vendor2t4r_2M_ota_20261009.cfg"), encoding="utf-8")
    assert int(cfg["spiPartition"]["count"]) == 5
    for index, expected in enumerate(PARTITIONS):
        assert cfg["spiPartition"][f"Partition{index}"].strip('"') == expected
    assert cfg["sfc"]["force_erase"] == "1"
    assert cfg["sfc"]["download_params"] == "1"
    assert int(cfg["sfc"]["download_params_offset"], 0) == 0x5800
    expected_paths = (out / SPL_NAME, out / "zero_ota.bin", out / "zero_ota.bin")
    for index, expected_path in enumerate(expected_paths):
        policy = cfg[f"policy{index}"]
        assert policy["enabled"] == "1"
        assert policy["label"] == ("uboot", "rtos_0", "rtos_1")[index]
        assert int(policy["offset"], 0) == (0, 0x6000, 0xF0000)[index]
        actual = policy["attribute"].replace("D:/", "/mnt/d/", 1) if out.as_posix().startswith("/mnt/d/") else policy["attribute"]
        assert Path(actual).resolve() == expected_path
    assert cfg["policy3"]["enabled"] == cfg["policy4"]["enabled"] == "0"
    for name, expected in manifest["files"].items():
        data = (out / name).read_bytes()
        assert len(data) == expected["bytes"]
        assert hashlib.sha256(data).hexdigest() == expected["sha256"]
    for relative, expected in manifest["source_sha256"].items():
        assert hashlib.sha256((source / relative).read_bytes()).hexdigest() == expected, relative
    print("PASS separate OTA SPL, both A/B slots, config boundaries, full erase and source/file hashes")


if __name__ == "__main__":
    main()
