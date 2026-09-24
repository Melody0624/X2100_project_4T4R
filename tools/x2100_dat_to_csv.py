#!/usr/bin/env python3
"""Parse X2100 MotorCycle Tools TLV DAT files and export CSV tables.

Supported result protocols:
* Legacy capture: 60-byte floating-point detections, 32-byte tracks,
  float32 ego velocity (MotorCycle Tools 2.4.1 recordings).
* Current compact protocol: 40-byte detections, 18-byte tracks,
  int16 ego velocity scaled by 100.

This script is for ordinary ``Record_*.dat`` result files. Raw
``Record_*_adc.dat`` files use TLV type 13 and must be handled by the ADC
parser instead.
"""

from __future__ import annotations

import argparse
import csv
import math
import struct
from pathlib import Path
from typing import Any, Iterable


MAGIC = bytes((0x02, 0x01, 0x04, 0x03, 0x06, 0x05, 0x08, 0x07))
HEADER = struct.Struct("<8sIIB3xIB3x")
TLV_HEADER = struct.Struct("<II")

TLV_DETECTIONS = 21
TLV_TRACKS = 22
TLV_EGO_VELOCITY = 23
TLV_WARNINGS = 24

LEGACY_FLOAT_DETECTION = struct.Struct("<IfffIffIfffffff")
COMPACT_DETECTION = struct.Struct("<HBBfffffhhhBBHHI")
LEGACY_TRACK_SIZE = 32
COMPACT_TRACK_SIZE = 18


class DatFormatError(RuntimeError):
    """Raised when a packet or TLV is truncated or inconsistent."""


def _finite(value: float) -> float | str:
    return value if math.isfinite(value) else ""


def _read_packets(data: bytes) -> Iterable[dict[str, Any]]:
    offset = 0
    file_index = 0
    while offset < len(data):
        if len(data) - offset < HEADER.size:
            raise DatFormatError(
                f"Trailing {len(data) - offset} bytes at file offset {offset}"
            )
        if data[offset : offset + len(MAGIC)] != MAGIC:
            next_offset = data.find(MAGIC, offset + 1)
            if next_offset < 0:
                raise DatFormatError(f"Magic word not found after byte {offset}")
            offset = next_offset

        magic, version, packet_length, platform, frame_number, num_tlvs = (
            HEADER.unpack_from(data, offset)
        )
        if magic != MAGIC:
            raise DatFormatError(f"Bad magic word at byte {offset}")
        if packet_length < HEADER.size:
            raise DatFormatError(
                f"Frame {frame_number} has invalid packet length {packet_length}"
            )
        packet_end = offset + packet_length
        if packet_end > len(data):
            raise DatFormatError(
                f"Frame {frame_number} ends at {packet_end}, beyond file size {len(data)}"
            )

        cursor = offset + HEADER.size
        tlvs: list[tuple[int, bytes]] = []
        for tlv_index in range(num_tlvs):
            if cursor + TLV_HEADER.size > packet_end:
                raise DatFormatError(
                    f"Frame {frame_number} TLV {tlv_index} header is truncated"
                )
            tlv_type, payload_length = TLV_HEADER.unpack_from(data, cursor)
            cursor += TLV_HEADER.size
            payload_end = cursor + payload_length
            if payload_end > packet_end:
                raise DatFormatError(
                    f"Frame {frame_number} TLV {tlv_type} payload is truncated"
                )
            tlvs.append((tlv_type, data[cursor:payload_end]))
            cursor = payload_end

        file_index += 1
        yield {
            "file_frame_index": file_index,
            "byte_offset": offset,
            "version": version,
            "packet_length": packet_length,
            "platform": platform,
            "frame_number": frame_number,
            "num_tlvs": num_tlvs,
            "tlvs": tlvs,
            "unparsed_packet_bytes": packet_end - cursor,
        }
        offset = packet_end


def _base_detection(frame: dict[str, Any], index: int, layout: str) -> dict[str, Any]:
    return {
        "file_frame_index": frame["file_frame_index"],
        "frame_number": frame["frame_number"],
        "detection_index": index,
        "wire_layout": layout,
    }


def _parse_detections(payload: bytes, frame: dict[str, Any]) -> tuple[list[dict[str, Any]], str]:
    if len(payload) < 2:
        raise DatFormatError(f"Frame {frame['frame_number']} detection TLV is too short")
    count = struct.unpack_from("<H", payload, 0)[0]
    rows: list[dict[str, Any]] = []

    if len(payload) == 2 + count * LEGACY_FLOAT_DETECTION.size:
        layout = "legacy-float60"
        cursor = 2
        for index in range(count):
            values = LEGACY_FLOAT_DETECTION.unpack_from(payload, cursor)
            cursor += LEGACY_FLOAT_DETECTION.size
            row = _base_detection(frame, index, layout)
            row.update(
                rel_rd_index=values[0], velocity_mps=_finite(values[1]),
                x_output_m=_finite(values[2]), y_output_m=_finite(values[3]),
                motion_state=values[4], power=_finite(values[5]),
                snr=_finite(values[6]), is_peak=values[7],
                range_m=_finite(values[8]),
                velocity_ambiguous_mps=_finite(values[9]),
                velocity_disamb_confidence=_finite(values[10]),
                velocity_disamb_factor=_finite(values[11]),
                azimuth_deg=_finite(values[12]), x_rcs_m=_finite(values[13]),
                y_rcs_m=_finite(values[14]),
            )
            rows.append(row)
        return rows, layout

    if len(payload) == 4 + count * COMPACT_DETECTION.size:
        layout = "compact-new40"
        cursor = 4
        for index in range(count):
            values = COMPACT_DETECTION.unpack_from(payload, cursor)
            cursor += COMPACT_DETECTION.size
            row = _base_detection(frame, index, layout)
            row.update(
                rel_rd_index=values[0], motion_state=values[1],
                is_peak=values[2], velocity_mps=_finite(values[3]),
                x_output_m=_finite(values[4]), y_output_m=_finite(values[5]),
                range_m=_finite(values[6]), azimuth_deg=_finite(values[7]),
                velocity_ambiguous_mps=values[8] / 100.0,
                x_rcs_m=values[9] / 100.0, y_rcs_m=values[10] / 100.0,
                velocity_disamb_confidence=values[11],
                velocity_disamb_factor=values[12], power=values[13] / 100.0,
                snr=values[14] / 100.0,
            )
            rows.append(row)
        return rows, layout

    if len(payload) == 2 + count * COMPACT_DETECTION.size:
        layout = "compact-legacy40"
        cursor = 2
        for index in range(count):
            item = payload[cursor : cursor + COMPACT_DETECTION.size]
            cursor += COMPACT_DETECTION.size
            row = _base_detection(frame, index, layout)
            row.update(
                rel_rd_index=struct.unpack_from("<H", item, 0)[0],
                velocity_mps=_finite(struct.unpack_from("<f", item, 4)[0]),
                x_output_m=_finite(struct.unpack_from("<f", item, 8)[0]),
                y_output_m=_finite(struct.unpack_from("<f", item, 12)[0]),
                motion_state=item[16],
                power=struct.unpack_from("<H", item, 18)[0] / 100.0,
                snr=struct.unpack_from("<H", item, 20)[0] / 100.0,
                is_peak=item[22],
                range_m=_finite(struct.unpack_from("<f", item, 24)[0]),
                velocity_ambiguous_mps=struct.unpack_from("<h", item, 28)[0] / 100.0,
                velocity_disamb_confidence=item[30],
                velocity_disamb_factor=item[31],
                azimuth_deg=_finite(struct.unpack_from("<f", item, 32)[0]),
                x_rcs_m=struct.unpack_from("<h", item, 36)[0] / 100.0,
                y_rcs_m=struct.unpack_from("<h", item, 38)[0] / 100.0,
            )
            rows.append(row)
        return rows, layout

    raise DatFormatError(
        f"Frame {frame['frame_number']} detection TLV length {len(payload)} "
        f"does not match count {count}"
    )


def _parse_tracks(payload: bytes, frame: dict[str, Any]) -> tuple[list[dict[str, Any]], str]:
    if not payload:
        raise DatFormatError(f"Frame {frame['frame_number']} track TLV is empty")
    rows: list[dict[str, Any]] = []

    legacy_count = payload[0]
    if len(payload) == 1 + legacy_count * LEGACY_TRACK_SIZE:
        layout = "legacy-float32"
        cursor = 1
        for index in range(legacy_count):
            item = payload[cursor : cursor + LEGACY_TRACK_SIZE]
            cursor += LEGACY_TRACK_SIZE
            rows.append({
                "file_frame_index": frame["file_frame_index"],
                "frame_number": frame["frame_number"],
                "track_index": index,
                "wire_layout": layout,
                "track_id": item[1],
                "is_valid": item[0],
                "motion_state": item[2],
                "x_output_m": _finite(struct.unpack_from("<f", item, 12)[0]),
                "y_output_m": _finite(struct.unpack_from("<f", item, 16)[0]),
                "vx_output_mps": _finite(struct.unpack_from("<f", item, 20)[0]),
                "vy_output_mps": _finite(struct.unpack_from("<f", item, 24)[0]),
                "heading_deg": _finite(struct.unpack_from("<f", item, 28)[0]),
                "max_length_m": _finite(struct.unpack_from("<f", item, 4)[0]),
                "max_width_m": _finite(struct.unpack_from("<f", item, 8)[0]),
                "measurement_counter": "",
            })
        return rows, layout

    if len(payload) >= 8:
        compact_count = payload[3]
        if len(payload) == 8 + compact_count * COMPACT_TRACK_SIZE:
            layout = "compact-new18"
            measurement_counter = struct.unpack_from("<I", payload, 4)[0]
            cursor = 8
            for index in range(compact_count):
                item = payload[cursor : cursor + COMPACT_TRACK_SIZE]
                cursor += COMPACT_TRACK_SIZE
                rows.append({
                    "file_frame_index": frame["file_frame_index"],
                    "frame_number": frame["frame_number"],
                    "track_index": index,
                    "wire_layout": layout,
                    "track_id": item[0],
                    "is_valid": item[11],
                    "motion_state": item[10],
                    "x_output_m": struct.unpack_from("<H", item, 2)[0] / 8.0 - 255.0,
                    "y_output_m": struct.unpack_from("<H", item, 4)[0] / 8.0 - 255.0,
                    "vx_output_mps": struct.unpack_from("<H", item, 6)[0] / 20.0 - 102.0,
                    "vy_output_mps": struct.unpack_from("<H", item, 8)[0] / 20.0 - 102.0,
                    "heading_deg": struct.unpack_from("<H", item, 14)[0] / 2.5 - 180.0,
                    "max_length_m": item[12] / 10.0,
                    "max_width_m": item[13] / 10.0,
                    "measurement_counter": measurement_counter,
                })
            return rows, layout

    raise DatFormatError(
        f"Frame {frame['frame_number']} track TLV length {len(payload)} is unsupported"
    )


def _parse_warning(payload: bytes, frame: dict[str, Any]) -> dict[str, Any]:
    row: dict[str, Any] = {
        "file_frame_index": frame["file_frame_index"],
        "frame_number": frame["frame_number"],
        "payload_bytes": len(payload),
        "payload_hex": payload.hex(),
    }
    if len(payload) >= 12:
        names = ("bsd_left", "bsd_right", "aoa_left", "aoa_right", "lca_left", "lca_right", "rcw")
        for index, name in enumerate(names):
            row[name] = payload[index]
        row["ttc_min_s"] = _finite(struct.unpack_from("<f", payload, 8)[0])
    if len(payload) >= 245:
        cursor = 12
        for name in ("bsd_left", "bsd_right", "aoa_left", "aoa_right", "lca_left", "lca_right", "rcw"):
            ids = payload[cursor : cursor + 32]
            count = min(payload[cursor + 32], 32)
            row[f"{name}_track_ids"] = "|".join(str(value) for value in ids[:count])
            cursor += 33
        row["lca_left_level"] = payload[cursor]
        row["lca_right_level"] = payload[cursor + 1]
    return row


def parse_dat(path: Path) -> dict[str, list[dict[str, Any]]]:
    data = path.read_bytes()
    frame_rows: list[dict[str, Any]] = []
    detection_rows: list[dict[str, Any]] = []
    track_rows: list[dict[str, Any]] = []
    warning_rows: list[dict[str, Any]] = []

    for frame in _read_packets(data):
        frame_detections: list[dict[str, Any]] = []
        frame_tracks: list[dict[str, Any]] = []
        detection_layout = ""
        track_layout = ""
        ego_velocity_mps: float | str = ""
        warning_bytes = 0
        unknown_types: list[int] = []

        for tlv_type, payload in frame["tlvs"]:
            if tlv_type == TLV_DETECTIONS:
                frame_detections, detection_layout = _parse_detections(payload, frame)
                detection_rows.extend(frame_detections)
            elif tlv_type == TLV_TRACKS:
                frame_tracks, track_layout = _parse_tracks(payload, frame)
                track_rows.extend(frame_tracks)
            elif tlv_type == TLV_EGO_VELOCITY:
                if len(payload) == 4:
                    ego_velocity_mps = _finite(struct.unpack("<f", payload)[0])
                elif len(payload) == 2:
                    ego_velocity_mps = struct.unpack("<h", payload)[0] / 100.0
                else:
                    raise DatFormatError(
                        f"Frame {frame['frame_number']} ego TLV length {len(payload)} is unsupported"
                    )
            elif tlv_type == TLV_WARNINGS:
                warning_bytes = len(payload)
                warning_rows.append(_parse_warning(payload, frame))
            else:
                unknown_types.append(tlv_type)

        frame_rows.append({
            "file_frame_index": frame["file_frame_index"],
            "byte_offset": frame["byte_offset"],
            "frame_number": frame["frame_number"],
            "version": frame["version"],
            "platform": frame["platform"],
            "packet_length": frame["packet_length"],
            "num_tlvs": frame["num_tlvs"],
            "num_detections": len(frame_detections),
            "num_tracks": len(frame_tracks),
            "ego_velocity_mps": ego_velocity_mps,
            "ego_velocity_kph": ego_velocity_mps * 3.6 if isinstance(ego_velocity_mps, float) else "",
            "detection_layout": detection_layout,
            "track_layout": track_layout,
            "warning_payload_bytes": warning_bytes,
            "unknown_tlv_types": "|".join(str(value) for value in unknown_types),
            "unparsed_packet_bytes": frame["unparsed_packet_bytes"],
        })

    return {
        "frames": frame_rows,
        "detections": detection_rows,
        "tracks": track_rows,
        "warnings": warning_rows,
    }


def _write_csv(path: Path, rows: list[dict[str, Any]], fallback_fields: list[str]) -> None:
    fields = list(rows[0].keys()) if rows else fallback_fields
    with path.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def export_csv(input_path: Path, output_dir: Path) -> dict[str, Path]:
    tables = parse_dat(input_path)
    output_dir.mkdir(parents=True, exist_ok=True)
    stem = input_path.stem
    paths = {
        name: output_dir / f"{stem}_{name}.csv"
        for name in ("frames", "detections", "tracks", "warnings")
    }
    _write_csv(paths["frames"], tables["frames"], ["file_frame_index", "frame_number"])
    _write_csv(paths["detections"], tables["detections"], ["file_frame_index", "frame_number", "detection_index"])
    _write_csv(paths["tracks"], tables["tracks"], ["file_frame_index", "frame_number", "track_index"])
    _write_csv(paths["warnings"], tables["warnings"], ["file_frame_index", "frame_number", "payload_hex"])

    print(f"Input: {input_path}")
    print(f"Frames: {len(tables['frames'])}")
    print(f"Detections: {len(tables['detections'])}")
    print(f"Tracks: {len(tables['tracks'])}")
    for name, path in paths.items():
        print(f"{name}: {path}")
    return paths


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Export X2100 MotorCycle Tools result DAT packets to CSV."
    )
    parser.add_argument("input", type=Path, help="Record_*.dat result file")
    parser.add_argument(
        "--output-dir", type=Path,
        help="CSV output directory (default: <input_stem>_csv beside the input file)",
    )
    args = parser.parse_args()
    input_path = args.input.resolve()
    if not input_path.is_file():
        parser.error(f"Input file does not exist: {input_path}")
    if input_path.name.lower().endswith("_adc.dat"):
        parser.error("This is a raw ADC file; use the ADC parser, not this result-TLV exporter")
    output_dir = args.output_dir.resolve() if args.output_dir else input_path.with_name(input_path.stem + "_csv")
    export_csv(input_path, output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
