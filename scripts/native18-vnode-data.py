#!/usr/bin/env python3
"""Identify native vnode descriptors by actual names and complete ABI fields.

Old APFS pointers must refer to the descriptors initialized by the source VFS.
This names matching objects; it never copies old offsets or fabricates tables.
All original addresses, source addresses and generated ABI metadata stay local.
"""
import argparse
from collections import defaultdict
from datetime import datetime
import hashlib
import json
from pathlib import Path
import struct
import sys


def offsets(image, pointer):
    if not pointer:
        return None
    values = []
    for index in range(16):
        value, = struct.unpack("<i", image.read(pointer + index * 4, 4))
        values.append(value)
        if value == -1:
            return values
        if value < 0 or value > 512 or value & 7:
            raise RuntimeError("Invalid vnode argument offset array")
    raise RuntimeError("Unterminated vnode argument offset array")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("prelink-kit", "stock-kernel", "native-kernel", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error("Retain prior descriptor evidence and choose a new output")
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image
    stock = Image(args.stock_kernel.read_bytes())
    original = Image(stock.data, stock.entries["com.apple.kernel"][1])
    native = Image(args.native_kernel.read_bytes())
    old_fix, new_fix = stock.fixups(), native.fixups()
    source_names = defaultdict(list)
    for address, symbols in native.symbols().items():
        for name in symbols:
            if name.startswith("_vnop_") and name.endswith("_desc"):
                source_names[name].append(address)
    originals = defaultdict(set)
    for site, pointer in old_fix.items():
        section = original.section(site)
        if not section or section["segment"] != "__DATA":
            continue
        name = original.cstring(pointer[0])
        if name and name.startswith("vnop_") and "_" + name + "_desc" in source_names:
            originals[name].add(site - 8)
    qualified, refused = [], []
    def fields(image, fixups, base):
        data = image.read(base, 64)
        current, flags = struct.unpack_from("<I", data, 0)[0], struct.unpack_from("<I", data, 16)[0]
        argument_offsets = struct.unpack_from("<5i", data, 32)
        name = image.cstring(fixups[base + 8][0])
        vp = fixups.get(base + 24)
        vp_offsets = offsets(image, vp[0]) if vp else None
        transports = fixups.get(base + 56)
        if transports or struct.unpack_from("<Q", data, 56)[0] != 0:
            raise RuntimeError("Nonempty vnode transport metadata requires separate review")
        return {"initial_offset": current, "flags": flags, "argument_offsets": list(argument_offsets),
                "name": name, "vp_offsets": vp_offsets, "transport": None}
    for name, addresses in sorted(originals.items()):
        symbol = "_" + name + "_desc"
        if len(addresses) != 1 or len(source_names[symbol]) != 1:
            refused.append({"name": name, "reason": "ambiguous original/source descriptor"})
            continue
        original_address, native_address = next(iter(addresses)), source_names[symbol][0]
        try:
            old_fields = fields(original, old_fix, original_address)
            source_fields = fields(native, new_fix, native_address)
            if old_fields != source_fields:
                refused.append({"name": name, "reason": "descriptor argument ABI differs",
                                "original_fields": old_fields, "source_fields": source_fields})
                continue
        except (ValueError, KeyError, RuntimeError) as error:
            refused.append({"name": name, "reason": str(error)})
            continue
        qualified.append({"original": hex(original_address), "name": symbol, "addend": 0,
                          "fields": old_fields, "evidence": "Unique chained vdesc_name and source global symbol; matching complete 64-byte descriptor scalars, five argument offsets, terminated vnode-offset array and null transport; source VFS owns runtime vdesc_offset"})
    report = {"build": "22G100", "arch": "arm64e", "analysis_only": True, "bootable": False,
              "observed_at": datetime.now().astimezone().isoformat(), "functions": [], "data": qualified,
              "refused": refused, "stock_sha256": hashlib.sha256(args.stock_kernel.read_bytes()).hexdigest(),
              "native_sha256": hashlib.sha256(args.native_kernel.read_bytes()).hexdigest()}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print("Vnode descriptors qualified", len(qualified), "refused", len(refused), "; local analysis only")
    if not qualified:
        raise SystemExit("No complete vnode descriptor ABI matched")


if __name__ == "__main__":
    main()
