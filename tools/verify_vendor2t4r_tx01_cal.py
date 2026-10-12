"""Check the linked calibration and the combined NOR image (no extra packages)."""
import argparse
import struct
from pathlib import Path

VALUES = (
    1.000000, 0.000000, -1.199234, 0.340255,
    -0.481211, -0.991089, 0.914203, 0.373536,
    0.413993, 1.169006, -0.884424, -1.231216,
    0.929756, -1.007362, -0.094489, 1.232397,
)


def symbol_bytes(elf, wanted):
    assert elf[:6] == b"\x7fELF\x01\x01", "Expected a little-endian ELF32"
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", elf)
    assert header[2] == 8, "Expected MIPS machine code"
    sections = [struct.unpack_from("<10I", elf, header[6] + i * header[11])
                for i in range(header[12])]
    for table in sections:
        if table[1] != 2:
            continue
        strings = sections[table[6]]
        names = elf[strings[4]:strings[4] + strings[5]]
        for offset in range(table[4], table[4] + table[5], table[9]):
            name, address, size, _, _, index = struct.unpack_from("<IIIBBH", elf, offset)
            if names[name:names.find(b"\0", name)].decode() != wanted:
                continue
            section = sections[index]
            start = section[4] + address - section[3]
            return elf[start:start + size]
    raise AssertionError(f"Missing ELF symbol: {wanted}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--elf-only", action="store_true")
    args = parser.parse_args()
    expected = struct.pack("<16f", *VALUES)
    linked = symbol_bytes((args.directory / "zero.elf").read_bytes(), "angCalibMat")
    assert linked == expected, "Linked angCalibMat differs from the supplied 8 complex coefficients"
    print("PASS: ELF contains the exact float32 coefficients, TX-major RX0..RX3")
    if args.elf_only:
        return
    combined = (args.directory / "rtos-with-spl.bin").read_bytes()
    payload = (args.directory / "zero.bin").read_bytes()
    assert combined[0x6000:] == payload, "Combined image payload mismatch"
    assert 0 < len(combined) <= 0x150000, "Image does not fit the firmware partition"
    assert expected in payload, "Calibration is absent from the burned payload"
    assert b"[CAL-TX01] compiled 8-channel compensation active" in payload
    cfg = (args.directory / "vendor2t4r_tx01_8ch_cal_2M.cfg").read_text(encoding="utf-8")
    assert 'Partition1="config,0x1DB000,0x25000,PART_RW,MTD_MODE"' in cfg
    assert "force_erase=0" in cfg and "download_params=1" in cfg
    assert "enabled=0\nlabel=config" in cfg
    windows_output = args.directory.resolve().as_posix().replace("/mnt/d/", "D:/", 1)
    assert f"attribute={windows_output}/rtos-with-spl.bin" in cfg
    print(f"PASS: combined image ({len(combined)} bytes), payload, config partition and erase policy")


if __name__ == "__main__":
    main()
