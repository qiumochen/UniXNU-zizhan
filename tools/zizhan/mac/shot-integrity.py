#!/usr/bin/env python3
"""Verify current chunked shot records against every pixel in shot.py PNGs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib


def png_rows(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("invalid PNG signature")
    offset, compressed, geometry = 8, bytearray(), None
    ended = False
    while offset + 12 <= len(data):
        length = int.from_bytes(data[offset:offset + 4], "big")
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        checksum = data[offset + 8 + length:offset + 12 + length]
        if len(payload) != length or len(checksum) != 4:
            raise ValueError("truncated PNG chunk")
        if zlib.crc32(kind + payload) & 0xffffffff != int.from_bytes(checksum, "big"):
            raise ValueError("PNG CRC mismatch")
        offset += length + 12
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            if (depth, color, compression, filtering, interlace) != (8, 2, 0, 0, 0):
                raise ValueError("expected shot.py RGB8 PNG")
            geometry = width, height
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            ended = True
            break
    if not ended or not geometry:
        raise ValueError("incomplete PNG")
    width, height = geometry
    raw = zlib.decompress(compressed)
    if len(raw) != height * (width * 3 + 1):
        raise ValueError("PNG row length mismatch")
    rows = []
    for row in range(height):
        start = row * (width * 3 + 1)
        if raw[start] != 0:
            raise ValueError("expected shot.py unfiltered rows")
        rows.append(raw[start + 1:start + 1 + width * 3])
    return width, height, rows


def verify(log_path, directory=None):
    text = log_path.read_bytes().decode("latin-1")
    shots = {}
    for number, width, height in re.findall(r"PONG SHOT (\d+) begin (\d+) (\d+) at \d+", text):
        geometry = int(width), int(height)
        if not all(0 < dimension <= 4096 for dimension in geometry):
            raise ValueError("invalid shot geometry")
        identifier = int(number)
        if identifier in shots and shots[identifier]["geometry"] != geometry:
            raise ValueError("conflicting shot headers")
        shots.setdefault(identifier, {"geometry": geometry, "pixels": {}})
    for number, row, column, payload in re.findall(r"PONG SHOT (\d+) (\d+) (\d+) ([0-9a-f]+)", text):
        shot = shots.get(int(number))
        if shot is None:
            continue
        width, height = shot["geometry"]
        row, column = int(row), int(column)
        if len(payload) % 6 or row >= height or column + len(payload) // 6 > width:
            raise ValueError("invalid or truncated shot chunk")
        chunk = bytes.fromhex(payload)
        for index in range(len(chunk) // 3):
            coordinate = row, column + index
            pixel = chunk[index * 3:index * 3 + 3]
            if coordinate in shot["pixels"] and shot["pixels"][coordinate] != pixel:
                raise ValueError("conflicting shot pixels")
            shot["pixels"][coordinate] = pixel
    results = []
    for identifier, shot in sorted(shots.items()):
        width, height = shot["geometry"]
        if len(shot["pixels"]) != width * height:
            raise ValueError(f"shot {identifier}: incomplete chunk coverage")
        path = (directory or log_path.parent) / f"{log_path.stem}.shot{identifier}.png"
        png_width, png_height, rows = png_rows(path)
        if (png_width, png_height) != (width * 3, height * 3):
            raise ValueError(f"shot {identifier}: PNG dimensions mismatch")
        pixels = bytearray()
        nonblack = 0
        for (row, column), pixel in sorted(shot["pixels"].items()):
            pixels.extend(pixel)
            nonblack += pixel != b"\0\0\0"
            for repeat in range(3):
                if rows[row * 3 + repeat][column * 9:column * 9 + 9] != pixel * 3:
                    raise ValueError(f"shot {identifier}: PNG differs from raw pixel record")
        results.append({"shot": identifier, "integrity": "PASS", "sample_pixels": width * height,
                        "nonblack_pixels": nonblack, "rgb_sha256": hashlib.sha256(pixels).hexdigest()})
    return {"status": "PASS" if results else "SKIP", "shots": results,
            "scope": "Raw-record/PNG file integrity only; content correctness and physical output require separate evidence."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--directory", type=Path)
    args = parser.parse_args()
    try:
        result = verify(args.log, args.directory)
    except (ValueError, OSError, struct.error, zlib.error) as error:
        print(json.dumps({"status": "FAIL", "scope": "capture file integrity", "reason": str(error)}))
        raise SystemExit(1)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
