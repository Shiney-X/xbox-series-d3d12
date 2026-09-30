#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

"""Compare two unscaled, game-only PNG captures without third-party packages."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys
import zlib


SIGNATURE = b"\x89PNG\r\n\x1a\n"
MAX_PIXELS = 32_000_000


def _chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload
            + struct.pack(">I", zlib.crc32(kind + payload)))


def write_rgb_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    if width <= 0 or height <= 0 or len(rgb) != width * height * 3:
        raise ValueError("invalid RGB image dimensions")
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3]
                    for y in range(height))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.write_bytes(SIGNATURE + _chunk(b"IHDR", ihdr)
                     + _chunk(b"IDAT", zlib.compress(rows)) + _chunk(b"IEND", b""))


def read_rgb_png(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(SIGNATURE):
        raise ValueError(f"{path}: not a PNG")
    offset = len(SIGNATURE)
    width = height = channels = 0
    idat = bytearray()
    seen_ihdr = seen_iend = False
    while offset + 12 <= len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        offset += 4
        kind = data[offset:offset + 4]
        offset += 4
        if offset + length + 4 > len(data):
            raise ValueError(f"{path}: truncated PNG chunk")
        payload = data[offset:offset + length]
        offset += length
        crc = struct.unpack_from(">I", data, offset)[0]
        offset += 4
        if zlib.crc32(kind + payload) != crc:
            raise ValueError(f"{path}: PNG CRC mismatch")
        if kind == b"IHDR":
            if seen_ihdr or length != 13:
                raise ValueError(f"{path}: invalid IHDR")
            seen_ihdr = True
            width, height, depth, color, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload)
            if (width <= 0 or height <= 0 or width * height > MAX_PIXELS
                    or depth != 8 or color not in (2, 6)
                    or compression != 0 or filtering != 0 or interlace != 0):
                raise ValueError(f"{path}: unsupported PNG; expected non-interlaced RGB/RGBA8")
            channels = 3 if color == 2 else 4
        elif kind == b"IDAT":
            if not seen_ihdr:
                raise ValueError(f"{path}: IDAT before IHDR")
            idat.extend(payload)
        elif kind == b"IEND":
            seen_iend = True
            break
        elif kind[0] & 0x20 == 0:
            raise ValueError(f"{path}: unsupported critical PNG chunk {kind!r}")
    if not seen_ihdr or not seen_iend or not idat:
        raise ValueError(f"{path}: incomplete PNG")
    stride = width * channels
    expected = height * (stride + 1)
    decompressor = zlib.decompressobj()
    raw = decompressor.decompress(bytes(idat), expected + 1)
    if len(raw) != expected or not decompressor.eof or decompressor.unused_data:
        raise ValueError(f"{path}: invalid PNG pixel stream")

    rgb = bytearray(width * height * 3)
    prior = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        filter_type = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        if filter_type > 4:
            raise ValueError(f"{path}: unsupported PNG filter {filter_type}")
        for i in range(stride):
            left = row[i - channels] if i >= channels else 0
            above = prior[i]
            upper_left = prior[i - channels] if i >= channels else 0
            if filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            elif filter_type == 4:
                p = left + above - upper_left
                distances = (abs(p - left), abs(p - above), abs(p - upper_left))
                predictor = (left, above, upper_left)[distances.index(min(distances))]
            else:
                predictor = 0
            row[i] = (row[i] + predictor) & 0xff
        for x in range(width):
            rgb_offset = (y * width + x) * 3
            rgb[rgb_offset:rgb_offset + 3] = row[x * channels:x * channels + 3]
        prior = row
    return width, height, bytes(rgb)


def compare(reference: tuple[int, int, bytes], candidate: tuple[int, int, bytes],
            pixel_threshold: int, max_changed_ratio: float,
            max_mean_error: float) -> tuple[dict[str, object], bytes]:
    width, height, expected = reference
    if candidate[:2] != (width, height):
        raise ValueError(f"dimension mismatch: reference {width}x{height}, "
                         f"candidate {candidate[0]}x{candidate[1]}")
    actual = candidate[2]
    changed = total_error = max_error = 0
    difference = bytearray(len(expected))
    for i in range(0, len(expected), 3):
        channels = [abs(expected[i + channel] - actual[i + channel]) for channel in range(3)]
        peak = max(channels)
        changed += peak > pixel_threshold
        total_error += sum(channels)
        max_error = max(max_error, peak)
        difference[i:i + 3] = bytes(channels)
    changed_ratio = changed / (width * height)
    mean_error = total_error / len(expected)
    result = {
        "width": width,
        "height": height,
        "changed_pixels": changed,
        "changed_ratio": changed_ratio,
        "mean_absolute_error": mean_error,
        "max_channel_error": max_error,
        "pixel_threshold": pixel_threshold,
        "max_changed_ratio": max_changed_ratio,
        "max_mean_error": max_mean_error,
        "passed": changed_ratio <= max_changed_ratio and mean_error <= max_mean_error,
    }
    return result, bytes(difference)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--pixel-threshold", type=int, default=0)
    parser.add_argument("--max-changed-ratio", type=float, default=0.0)
    parser.add_argument("--max-mean-error", type=float, default=0.0)
    parser.add_argument("--report", type=Path, help="write JSON result")
    parser.add_argument("--diff", type=Path, help="write RGB difference PNG")
    args = parser.parse_args()
    if (not 0 <= args.pixel_threshold <= 255 or not 0 <= args.max_changed_ratio <= 1
            or not 0 <= args.max_mean_error <= 255):
        parser.error("thresholds must be within pixel [0,255], ratio [0,1], mean [0,255]")
    outputs = [path.resolve() for path in (args.report, args.diff) if path is not None]
    inputs = {args.reference.resolve(), args.candidate.resolve()}
    if len(outputs) != len(set(outputs)) or inputs.intersection(outputs):
        parser.error("report and diff must be distinct from each other and the input images")
    try:
        reference = read_rgb_png(args.reference)
        candidate = read_rgb_png(args.candidate)
        result, difference = compare(reference, candidate, args.pixel_threshold,
                                     args.max_changed_ratio, args.max_mean_error)
        if args.diff:
            write_rgb_png(args.diff, reference[0], reference[1], difference)
        report = json.dumps(result, indent=2, sort_keys=True)
        if args.report:
            args.report.write_text(report + "\n", encoding="utf-8")
        print(report)
        return 0 if result["passed"] else 1
    except (OSError, ValueError, zlib.error) as error:
        print(f"compare_frames: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
