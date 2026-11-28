#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Decode ZIZHAN_CAM_PREVIEW RGB24 base64 blocks from a klog text file into PNGs."""
import base64
import re
import struct
import sys
import zlib
from pathlib import Path


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    if len(rgb) != width * height * 3:
        raise ValueError(f"RGB length mismatch: {len(rgb)} != {width * height * 3}")
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)
        raw.extend(rgb[y * stride : (y + 1) * stride])

    def chunk(tag: bytes, data: bytes) -> bytes:
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )
    path.write_bytes(png)


def main() -> None:
    if len(sys.argv) < 3:
        raise SystemExit("usage: decode-cam-preview.py <klog.txt> <out.png>")
    log_path = Path(sys.argv[1])
    out_base = Path(sys.argv[2])
    text = log_path.read_text(errors="replace")
    pattern = re.compile(
        r"ZIZHAN_CAM_PREVIEW_BEGIN: tag=(\S+) w=(\d+) h=(\d+) fmt=RGB24 bytes=(\d+)\n(.*?)ZIZHAN_CAM_PREVIEW_END: tag=\1 lines=(\d+)",
        re.DOTALL,
    )
    matches = list(pattern.finditer(text))
    if not matches:
        raise SystemExit("no complete ZIZHAN_CAM_PREVIEW block found")
    for idx, m in enumerate(matches):
        tag, w, h, total = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))
        body, expected_lines = m.group(5), int(m.group(6))
        chunks = {}
        for seq_s, b64_s in re.findall(r"ZIZHAN_CAM_PREVIEW:\s+(\d+)\s+([A-Za-z0-9+/=]+)", body):
            chunks[int(seq_s)] = b64_s
        missing = [i for i in range(expected_lines) if i not in chunks]
        b64_all = "".join(chunks[i] for i in sorted(chunks.keys()))
        rgb = base64.b64decode(b64_all)
        rgb = (rgb + b"\x00" * max(0, total - len(rgb)))[:total]
        tagged_out = out_base.with_name(f"{out_base.stem}-{tag}.png")
        write_png(tagged_out, w, h, rgb)
        print(
            f"[{idx}] tag={tag} {w}x{h} bytes={len(rgb)}/{total} lines={len(chunks)}/{expected_lines} missing={len(missing)} -> {tagged_out}"
        )
        if idx == 0:
            write_png(out_base, w, h, rgb)


if __name__ == "__main__":
    main()
