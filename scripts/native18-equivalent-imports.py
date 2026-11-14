#!/usr/bin/env python3
"""Qualify complete identical-body wrappers with resolved call/data operands.

The ordinary matcher intentionally refuses nonunique bodies. Multiple source
definitions may nonetheless have identical observable instructions, including
every concrete callee and global operand. This only names such complete real
bodies, with compatible incoming PAC contracts. No source code or stub is made.
"""
import argparse
from collections import defaultdict
from datetime import datetime
import hashlib
import json
from pathlib import Path
import struct
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("prelink-kit", "stock-kernel", "native-kernel", "pairing", "imports", "out"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    if args.out.exists():
        parser.error("Retain previous evidence and select a new output")
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan, signed
    stock = Image(args.stock_kernel.read_bytes())
    original = Image(stock.data, stock.entries["com.apple.kernel"][1])
    native = Image(args.native_kernel.read_bytes())
    mapping, imports = json.loads(args.pairing.read_text()), json.loads(args.imports.read_text())
    if (mapping.get("build"), mapping.get("arch")) != ("22G100", "arm64e") or Path(mapping["native"]).resolve() != args.native_kernel.resolve() or Path(mapping["original"]).resolve() != args.stock_kernel.resolve():
        parser.error("Fresh pairing for the actual stock/native input paths is required")
    functions = {int(key, 16): int(value["native"], 16) for key, value in mapping["functions"].items()}
    data = {int(key, 16): int(value["native"], 16) for key, value in mapping["data"].items()}
    missing = {int(key, 16) for rows in imports["missing"].values() for key in rows}
    def bodies(image):
        ranges = [(row["va"], row["va"] + row["filesize"]) for row in image.segments
                  if row["name"] in ("__TEXT_EXEC", "__TEXT_BOOT_EXEC", "__KLD", "__LAST") and row["filesize"]]
        starts = sorted(address for address in image.function_starts()
                        if any(first <= address < end for first, end in ranges))
        result = {}
        for index, address in enumerate(starts):
            end = min([last for first, last in ranges if first <= address < last] +
                      ([starts[index + 1]] if index + 1 < len(starts) else []))
            if not 12 <= end - address <= 1024:
                continue
            code = image.read(address, end - address)
            calls, detailed_refs, shape = scan(code, address, with_origins=True)
            # A masked ADRP must have an actual resolved consumer. Unused
            # page addresses are observable too; do not mask them away.
            used_pages = {ref[3] for ref in detailed_refs if ref[3] is not None}
            safe = True
            for offset in range(0, len(code), 4):
                word, = struct.unpack_from('<I', code, offset)
                if word & 0x9f000000 == 0x90000000 and address + offset not in used_pages:
                    safe = False
                # Literal loads have PC-relative data not tracked by scan.
                if word & 0x3b000000 == 0x18000000:
                    safe = False
                # Keep conditional branches only when the target is inside
                # this complete body. Raw encoding equality then proves the
                # same internal geometry, not an unexamined external edge.
                if word & 0xff000010 == 0x54000000 or word & 0x7e000000 == 0x34000000:
                    destination = offset + signed((word >> 5) & 0x7ffff, 19) * 4
                    safe &= 0 <= destination < len(code)
                if word & 0x7e000000 == 0x36000000:
                    destination = offset + signed((word >> 5) & 0x3fff, 14) * 4
                    safe &= 0 <= destination < len(code)
            if not safe:
                continue
            refs = [ref[:3] for ref in detailed_refs]
            result[address] = {"end": end, "calls": calls, "refs": refs, "shape": tuple(shape), "code": code}
        return result
    left, right = bodies(original), bodies(native)
    pool = defaultdict(list)
    for address, row in right.items():
        pool[row["shape"]].append(address)
    names = native.symbols()
    symbol_addresses = defaultdict(set)
    for address, symbols in names.items():
        for symbol in symbols:
            symbol_addresses[symbol].add(address)
    unique_names = {address: [symbol for symbol in symbols if len(symbol_addresses[symbol]) == 1
                             and not any(part in symbol for part in (".llvm.", ".cold.", "OUTLINED_FUNCTION"))]
                    for address, symbols in names.items()}
    old_types, new_types = defaultdict(set), defaultdict(set)
    for _, pointer in stock.fixups().items():
        if pointer[1]:
            old_types[pointer[0]].add(pointer[1:])
    for _, pointer in native.fixups().items():
        if pointer[1]:
            new_types[pointer[0]].add(pointer[1:])
    qualified = []
    for address in sorted(missing & left.keys()):
        row = left[address]
        # The original pure-leaf matcher excludes memory operations. A
        # complete byte-identical local leaf can nevertheless have the same
        # concrete load/store behavior without an external operand. Require
        # byte equality as well as complete geometry/PAC checks in that case.
        local_leaf = not row["calls"] and not row["refs"]
        candidates = []
        for target in pool[row["shape"]]:
            other = right[target]
            if len(row["calls"]) != len(other["calls"]) or len(row["refs"]) != len(other["refs"]):
                continue
            # Stock fileset GOT/stub transport uses IA, diversity zero,
            # address diversity. It authenticates the pointer slot, without
            # encoding a C type. The emitter must re-sign the destination
            # slot. Require any actual typed contracts independently.
            transport = {(1, 0, 0, 1)}
            if not (old_types[address] - transport) <= new_types[target]:
                continue
            if not unique_names.get(target):
                continue
            if local_leaf and row["code"] != other["code"]:
                continue
            valid, contracts = True, []
            for (site, operand, kind), (new_site, new_operand, new_kind) in zip(row["refs"], other["refs"]):
                if site - address != new_site - target or kind != new_kind:
                    valid = False
                    break
                if operand in data and data[operand] == new_operand:
                    contracts.append({"kind": "mapped-data", "original": hex(operand), "native": hex(new_operand)})
                elif original.cstring(operand) is not None and original.cstring(operand) == native.cstring(new_operand):
                    contracts.append({"kind": "identical-string", "bytes": len(original.cstring(operand))})
                else:
                    valid = False
                    break
            if not valid:
                continue
            for (site, callee, link), (new_site, new_callee, new_link) in zip(row["calls"], other["calls"]):
                if site - address != new_site - target or link != new_link:
                    valid = False
                    break
                if address <= callee < row["end"]:
                    if callee - address != new_callee - target:
                        valid = False
                        break
                elif functions.get(callee) == new_callee:
                    contracts.append({"kind": "mapped-callee", "original": hex(callee), "native": hex(new_callee)})
                else:
                    valid = False
                    break
            if local_leaf:
                contracts.append({"kind": "complete-byte-identical-local-body", "bytes": len(row["code"]),
                                  "external_calls": 0, "pc_relative_data_operands": 0})
            if valid and contracts:
                candidates.append((target, contracts))
        if not candidates:
            continue
        # Every accepted candidate has identical raw normalized instructions
        # and the same resolved concrete operands; preserve all alternatives.
        # Use a genuine externally named definition, never a synthesized body.
        target, contracts = min(candidates, key=lambda item: (min(unique_names[item[0]]), item[0]))
        symbol = min(unique_names[target])
        qualified.append({"original": hex(address), "name": symbol,
                          "operational_equivalence_only": True,
                          "evidence": "Complete relocation-masked instruction body identical; every external call/data operand independently mapped or same literal, internal branch geometry identical; typed incoming PAC contracts match, IA/diversity-zero/address-diverse stock GOT transport is recorded separately; source alternatives are exact operational duplicates",
                          "code_bytes": len(row["code"]), "original_sha256": hashlib.sha256(row["code"]).hexdigest(),
                          "native_sha256": hashlib.sha256(right[target]["code"]).hexdigest(),
                          "original_pac_contracts": sorted(old_types[address]),
                          "native_pac_contracts": sorted(new_types[target]),
                          "contracts": contracts,
                          "equivalent_source_definitions": [{"names": names[value], "address": hex(value)} for value, _ in candidates]})
    report = {"build": "22G100", "arch": "arm64e", "analysis_only": True, "bootable": False,
              "observed_at": datetime.now().astimezone().isoformat(), "functions": qualified, "data": [],
              "stock_sha256": hashlib.sha256(args.stock_kernel.read_bytes()).hexdigest(),
              "native_sha256": hashlib.sha256(args.native_kernel.read_bytes()).hexdigest(),
              "pairing_sha256": hashlib.sha256(args.pairing.read_bytes()).hexdigest()}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print("Complete resolved-operand wrappers qualified:", len(qualified), "; local import evidence only")


if __name__ == "__main__":
    main()
