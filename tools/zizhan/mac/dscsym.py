#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Symbolize user crash frames from a device log against an extracted dyld shared cache. Host only (macOS: uses atos).

Usage: dscsym.py LOG DSC_DIR [--proc NAME]
       dscsym.py --addr 0x1ee5cbb50 --slide 0x11d20000 DSC_DIR

LOG lines come from the XNU crash capture (pong_ui_exception.c, pong_crash_name=<proc>):
  "Pong UI crash: kbd[122] exception=1 code=0x1/0x0 pc=0x... lr=0x... sp=0x... cache_slide=0x..." then "frame<N> lr=0x..."
DSC_DIR is a dsc_extractor output tree (each dylib keeps its unslid cache address). The first run indexes every Mach-O's
__TEXT range into DSC_DIR/.dscsym-index.json; later runs reuse it.
"""
import json
import os
import re
import struct
import subprocess
import sys

MH_MAGIC_64, LC_SEGMENT_64 = 0xFEEDFACF, 0x19


def text_range(path):
    try:
        with open(path, "rb") as f:
            head = f.read(32)
            if len(head) < 32 or struct.unpack_from("<I", head)[0] != MH_MAGIC_64:
                return None
            ncmds, sizeofcmds = struct.unpack_from("<II", head, 16)
            cmds = f.read(sizeofcmds)
    except OSError:
        return None
    off = 0
    for _ in range(ncmds):
        if off + 8 > len(cmds):
            break
        cmd, size = struct.unpack_from("<II", cmds, off)
        if cmd == LC_SEGMENT_64 and cmds[off + 8:off + 24].rstrip(b"\0") == b"__TEXT":
            vmaddr, vmsize = struct.unpack_from("<QQ", cmds, off + 24)
            return vmaddr, vmsize
        off += size
    return None


def index(dsc):
    cache = os.path.join(dsc, ".dscsym-index.json")
    if os.path.exists(cache):
        return json.load(open(cache))
    rows = []
    for root, _, files in os.walk(dsc):
        for name in files:
            path = os.path.join(root, name)
            r = text_range(path)
            if r and r[0] >= 0x180000000:
                rows.append([r[0], r[0] + r[1], os.path.relpath(path, dsc)])
    rows.sort()
    json.dump(rows, open(cache, "w"))
    return rows


def find(rows, addr):
    lo, hi = 0, len(rows)
    while lo < hi:
        mid = (lo + hi) // 2
        if rows[mid][0] <= addr:
            lo = mid + 1
        else:
            hi = mid
    if lo and rows[lo - 1][0] <= addr < rows[lo - 1][1]:
        return rows[lo - 1]
    return None


def symbolize(dsc, rows, addrs):
    out = {}
    by_lib = {}
    for a in addrs:
        r = find(rows, a)
        if r:
            by_lib.setdefault(r[2], (r[0], []))[1].append(a)
        else:
            out[a] = "?"
    for lib, (base, items) in by_lib.items():
        res = subprocess.run(["atos", "-o", os.path.join(dsc, lib), "-l", hex(base)] + [hex(a) for a in items],
                             capture_output=True, text=True)
        names = res.stdout.splitlines()
        for a, n in zip(items, names + ["?"] * len(items)):
            out[a] = f"{os.path.basename(lib)}: {n}"
    return out


def main():
    args = sys.argv[1:]
    if "--addr" in args:
        a = int(args[args.index("--addr") + 1], 16)
        slide = int(args[args.index("--slide") + 1], 16)
        dsc = [x for x in args if not x.startswith("--") and not x.startswith("0x")][-1]
        rows = index(dsc)
        print(symbolize(dsc, rows, [a - slide])[a - slide])
        return
    if len(args) < 2:
        sys.exit(__doc__)
    log, dsc = args[0], args[1]
    want = args[args.index("--proc") + 1] if "--proc" in args else None
    rows = index(dsc)
    t = open(log, "rb").read().decode("latin1")
    crashes = []
    for m in re.finditer(r"Pong UI crash: (\S+)\[(\d+)\] exception=(\d+) code=(\S+) pc=0x([0-9a-f]+) lr=0x([0-9a-f]+) "
                         r"sp=0x[0-9a-f]+ cache_slide=0x([0-9a-f]+)", t):
        name = m.group(1)
        if want and name != want:
            continue
        slide = int(m.group(7), 16)
        frames = []
        rest = t[m.end():m.end() + 4000]
        for f in re.finditer(r"Pong UI crash: frame(\d+) lr=0x([0-9a-f]+)", rest):
            if int(f.group(1)) != len(frames):
                break
            frames.append(int(f.group(2), 16))
        crashes.append((f"{name}[{m.group(2)}] exception={m.group(3)} code={m.group(4)}", slide,
                        [("pc", int(m.group(5), 16)), ("lr", int(m.group(6), 16))] + [(f"#{i}", x) for i, x in enumerate(frames)]))
    if not crashes:
        sys.exit("no 'Pong UI crash' records in the log")
    seen = set()
    for title, slide, regs in crashes:
        key = tuple(x for _, x in regs)
        if key in seen:
            continue
        seen.add(key)
        names = symbolize(dsc, rows, [x - slide for _, x in regs])
        print(f"== {title} slide=0x{slide:x}")
        for label, x in regs:
            print(f"  {label:4s} 0x{x:x} (0x{x - slide:x})  {names[x - slide]}")


if __name__ == "__main__":
    main()
