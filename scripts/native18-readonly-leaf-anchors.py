#!/usr/bin/env python3
"""Qualify read-only leaf imports by complete concrete register/memory effects.

Only a bounded whitelist is interpreted. Scratch-register choice and a
pre-indexed load can vary; every input-derived address, width, read order,
typed AUTDA contract and return expression must remain identical. Field
offsets are never discarded. Unknown instructions/calls/branches refuse.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import struct
import sys


def plus(base, offset):
    if not offset:
        return base
    if base[0] == 'add':
        return plus(base[1], base[2] + offset)
    return ('add', base, offset)


def meaning(code, pc, checked_shape):
    words, _, _, _ = checked_shape(code, pc)
    if not words or words[-1] != 0xd65f03c0:
        return None
    registers = {i: ('input', i) for i in range(31)}
    registers[31] = ('constant', 0)
    reads, writes = [], set()
    for word in words[:-1]:
        if word in (0xd503245f, 0xd503241f, 0xd503249f, 0xd50324df):
            continue
        destination, base = word & 31, (word >> 5) & 31
        if destination >= 18:
            return None
        if word & 0xffe0ffe0 == 0xaa0003e0:  # mov Xd, Xm
            registers[destination] = registers[(word >> 16) & 31]
        elif word & 0xffc00000 == 0x91000000:  # add Xd,Xn,#imm12
            if base == 31:
                return None
            registers[destination] = plus(registers[base], (word >> 10) & 0xfff)
        elif word & 0xffc00000 in (0x39400000, 0x79400000, 0xb9400000, 0xf9400000):
            if base == 31:
                return None
            bits = 8 << (word >> 30)
            address = plus(registers[base], ((word >> 10) & 0xfff) * (bits // 8))
            value = ('load', bits, address)
            reads.append(value)
            registers[destination] = value
        elif word & 0xffe00c00 == 0xf8400c00:  # ldr Xd,[Xn,#simm9]!
            if base >= 18 or base == destination:
                return None
            immediate = (word >> 12) & 0x1ff
            if immediate & 0x100:
                immediate -= 0x200
            registers[base] = plus(registers[base], immediate)
            writes.add(base)
            value = ('load', 64, registers[base])
            reads.append(value)
            registers[destination] = value
        elif word & 0xffe00000 == 0xf2e00000:  # movk Xd,#diversity,lsl48
            registers[destination] = ('insert48', registers[destination], (word >> 5) & 0xffff)
        elif word == 0xdac11a30:  # AUTDA X16,X17
            registers[16] = ('autda', registers[16], registers[17])
            writes.add(16)
            continue
        elif word & 0xffc00000 in (0x53000000, 0xd3400000):  # unsigned extraction
            first, last = (word >> 16) & 63, (word >> 10) & 63
            width = 64 if word >> 31 else 32
            if first > last or last >= width:
                return None
            registers[destination] = ('extract', registers[base], first, last - first + 1)
        else:
            return None
        writes.add(destination)
    if not reads or 0 not in writes or any(register >= 18 for register in writes):
        return None
    return (registers[0], tuple(reads))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'pairing', 'imports', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain old evidence; choose a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    from fileset_shape import checked_shape
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    pairing = json.loads(args.pairing.read_text())
    if pairing.get('build') != '22G100' or Path(pairing['original']).resolve() != args.stock_kernel.resolve() or Path(pairing['native']).resolve() != args.native_kernel.resolve():
        parser.error('Require actual-input fresh pairing')
    imports = json.loads(args.imports.read_text())
    missing = {int(key, 16) for rows in imports['missing'].values() for key in rows}
    def bodies(image):
        starts = sorted(a for a in image.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in image.segments))
        result = {}
        for first, last in zip(starts, starts[1:]):
            if not 8 <= last - first <= 128:
                continue
            code = image.read(first, last - first)
            calls, refs, _ = scan(code, first)
            if refs or any(not first <= target < last for _, target, _ in calls):
                continue
            signature = meaning(code, first, checked_shape)
            if signature:
                result[first] = (signature, code)
        return result
    old, new = bodies(original), bodies(native)
    symbols = native.symbols()
    names = defaultdict(set)
    for address, rows in symbols.items():
        for name in rows:
            names[name].add(address)
    old_pac, new_pac = defaultdict(set), defaultdict(set)
    for pointer in cache.fixups().values():
        if pointer[1]:
            old_pac[pointer[0]].add(pointer[1:])
    for pointer in native.fixups().values():
        if pointer[1]:
            new_pac[pointer[0]].add(pointer[1:])
    pool = defaultdict(list)
    for address, (signature, code) in new.items():
        named = [name for name in symbols.get(address, []) if len(names[name]) == 1 and not any(part in name for part in ('.cold.', '.llvm.', 'OUTLINED_FUNCTION'))]
        if named:
            pool[signature].append((address, min(named)))
    rows = []
    for address in sorted(missing & old.keys()):
        signature, code = old[address]
        matches = [row for row in pool[signature] if old_pac[address] - {(1, 0, 0, 1)} <= new_pac[row[0]]]
        if not matches:
            continue
        target, name = min(matches, key=lambda row: row[1])
        rows.append({'original': hex(address), 'name': name, 'final_import_only': True,
                     'evidence': 'Complete bounded read-only leaf interpreted from a strict instruction whitelist; identical ordered memory reads, concrete field offsets/widths, typed AUTDA addresses/diversity and return expression; only verified complete compiler PAC-check shell normalized',
                     'original_sha256': hashlib.sha256(code).hexdigest(), 'native_sha256': hashlib.sha256(new[target][1]).hexdigest(),
                     'concrete_effect': signature, 'alternatives': [{'address': hex(a), 'name': n} for a, n in matches]})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'build': '22G100', 'arch': 'arm64e',
                                   'stock_sha256': hashlib.sha256(args.stock_kernel.read_bytes()).hexdigest(),
                                   'native_sha256': hashlib.sha256(args.native_kernel.read_bytes()).hexdigest(),
                                   'functions': rows, 'data': [], 'bootable': False}, indent=2) + '\n')
    print('Complete concrete read-only leaf imports qualified', len(rows), '; no offset relaxation')


if __name__ == '__main__':
    main()
