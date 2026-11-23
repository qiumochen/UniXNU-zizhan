#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Pack the files the sensor DSP reads through apps_std into the seed archive of drivers/PongPower/ZizhanSlpiStd.h.

Usage: sensors-fs-pack.py ROOT OUT [--list]

ROOT mirrors absolute device paths (ROOT/vendor/etc/sensors/sns_reg_config, ROOT/vendor/etc/sensors/config/*.json,
ROOT/sys/devices/soc0/hw_platform ...): the vendor's own files, pulled from the phone's Android, never committed. The archive
goes into the ADT as /chosen zizhan-sensors-fs (make-pong-adt-ipados18.py --chosen-blob zizhan-sensors-fs=OUT).
Format: "ZSF1", u32 count, then per file u16 path length, u16 0, u32 data length, the path, the data.
"""
import struct
import sys
from pathlib import Path


def pack(root):
    files = sorted(p for p in root.rglob("*") if p.is_file() and not p.name.startswith("."))
    out = bytearray(b"ZSF1" + struct.pack("<I", len(files)))
    for p in files:
        name = ("/" + p.relative_to(root).as_posix()).encode()
        if len(name) >= 160:
            sys.exit(f"path too long for the DSP file table: {name.decode()}")
        data = p.read_bytes()
        out += struct.pack("<HHI", len(name), 0, len(data)) + name + data
    return files, bytes(out)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    root = Path(sys.argv[1])
    files, blob = pack(root)
    if not files:
        sys.exit(f"no files under {root}")
    if len(files) > 900:
        sys.exit("more files than the DSP file table holds (1024 nodes including directories)")
    Path(sys.argv[2]).write_bytes(blob)
    if "--list" in sys.argv:
        for p in files:
            print(f"{p.stat().st_size:8d} /{p.relative_to(root).as_posix()}")
    print(f"{len(files)} files, {len(blob)} bytes -> {sys.argv[2]}")


if __name__ == "__main__":
    main()
