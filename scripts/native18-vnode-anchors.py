#!/usr/bin/env python3
"""Qualify three exact native default-vnode operations for import pairing.

These real source definitions may be absent from the source's own vnode tables.
Require independent same-Darwin naming, identical complete code, no PC-relative
operands/calls, and the original VOPFUNC authentication contract before naming.
Writes only local analysis metadata; does not alter either kernel.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import struct
import sys


def body(image, address):
    starts = image.function_starts()
    if address not in starts:
        raise RuntimeError("Vnode default is not a function entry")
    end = min(point for point in starts if point > address)
    code = image.read(address, end - address)
    while len(code) > 4 and code[-4:] == struct.pack("<I", 0xd503201f):
        code = code[:-4]
    if not 8 <= len(code) <= 64 or code[-4:] != struct.pack("<I", 0xd65f03c0):
        raise RuntimeError("Review changed default vnode operation body")
    return code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("prelink-kit", "stock-kernel", "native-kernel", "name-reference", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error("Retain existing anchors and choose a new output file")
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    stock = Image(args.stock_kernel.read_bytes())
    original = Image(stock.data, stock.entries["com.apple.kernel"][1])
    native = Image(args.native_kernel.read_bytes())
    reference = json.loads(args.name_reference.read_text())
    if reference.get("build") != "22G100" or reference.get("reference_role") != "names-only":
        parser.error("Same-release independent naming reference required")
    names = {}
    for address, symbols in native.symbols().items():
        for name in symbols:
            names.setdefault(name, []).append(address)
    rows = []
    pointers = stock.fixups()
    native_pointers = native.fixups()
    for name in ("_err_readdirattr", "_nop_allocate", "_err_searchfs"):
        identities = [row for row in reference["functions"] if name in row["names"] and row.get("evidence")]
        if len(identities) != 1 or len(names.get(name, [])) != 1:
            raise RuntimeError("Ambiguous default vnode identity: " + name)
        address, target = int(identities[0]["original"], 16), names[name][0]
        old_code, new_code = body(original, address), body(native, target)
        if old_code != new_code:
            raise RuntimeError("Source/stock default vnode code differs: " + name)
        calls, refs, _ = scan(old_code, address)
        if calls or refs:
            raise RuntimeError("Default vnode has a call or relocated reference: " + name)
        contracts = {pointer[1:] for pointer in pointers.values() if pointer[0] == address}
        if contracts != {(1, 0, 12410, 0)} or any(pointer[0] == target for pointer in native_pointers.values()):
            raise RuntimeError("Default vnode pointer tables changed: " + name)
        rows.append({"original": hex(address), "name": name, "pointer_contract": "VOPFUNC:int(void*)",
                     "code_bytes": len(old_code), "code_sha256": hashlib.sha256(old_code).hexdigest(),
                     "evidence": "Independent same-Darwin name; complete source and stock instructions identical, no PC-relative operand/call; original only VOPFUNC diversity12410; source has no static pointer slot"})
    report = {"build": "22G100", "arch": "arm64e", "analysis_only": True, "bootable": False,
              "observed_at": datetime.now().astimezone().isoformat(), "functions": rows,
              "stock_sha256": hashlib.sha256(args.stock_kernel.read_bytes()).hexdigest(),
              "native_sha256": hashlib.sha256(args.native_kernel.read_bytes()).hexdigest()}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print("Three exact default vnode function/PAC contracts qualified; analysis metadata only")


if __name__ == "__main__":
    main()
