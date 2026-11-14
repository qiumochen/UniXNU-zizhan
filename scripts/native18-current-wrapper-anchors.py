#!/usr/bin/env python3
"""Qualify complete wrappers that inline one pure current-thread accessor.

The stock body reads a system register then tail-calls an already paired
routine. The actual source body calls a pure three-instruction accessor and
uses the complete balanced PAC frame/return-check shell before the same
tail-call. Every non-frame register effect and the concrete sysreg agree.
No arbitrary instruction stripping, field offsets or new functions.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import struct
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'pairing', 'imports', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain previous evidence; choose a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    pairing = json.loads(args.pairing.read_text())
    if pairing.get('build') != '22G100' or Path(pairing['original']).resolve() != args.stock_kernel.resolve() or Path(pairing['native']).resolve() != args.native_kernel.resolve():
        parser.error('Require fresh actual-input pairing')
    imports = json.loads(args.imports.read_text())
    missing = {int(key, 16) for rows in imports['missing'].values() for key in rows}
    def bodies(image):
        starts = sorted(a for a in image.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in image.segments))
        return {first: image.read(first, last - first) for first, last in zip(starts, starts[1:]) if 8 <= last - first <= 64}
    old, new = bodies(original), bodies(native)
    symbols = native.symbols()
    names = defaultdict(set)
    for address, rows in symbols.items():
        for name in rows:
            names[name].add(address)
    incoming = defaultdict(set)
    for pointer in cache.fixups().values():
        if pointer[1]:
            incoming[pointer[0]].add(pointer[1:])
    typed = defaultdict(set)
    for pointer in native.fixups().values():
        if pointer[1]:
            typed[pointer[0]].add(pointer[1:])
    candidates = defaultdict(list)
    expected_shell = (0xd503237f, 0xa9bf7bfd, 0x910003fd, 0x94000000, 0xa8c17bfd,
                      0xd50323ff, 0xca1e07d0, 0xb6f00050, 0xd4388e20, 0x14000000)
    for address, code in new.items():
        if len(code) != 40:
            continue
        calls, refs, shape = scan(code, address)
        if tuple(shape) != expected_shell or refs or len(calls) != 2 or calls[0][0] != address + 12 or not calls[0][2] or calls[1][0] != address + 36 or calls[1][2]:
            continue
        helper, tail = calls[0][1], calls[1][1]
        if helper not in new or len(new[helper]) != 12:
            continue
        words = struct.unpack('<3I', new[helper])
        if words[0] != 0xd503245f or words[2] != 0xd65f03c0 or words[1] & 0xfff0001f != 0xd5300000:
            continue
        names_here = [name for name in symbols.get(address, []) if len(names[name]) == 1 and not any(part in name for part in ('.llvm.', '.cold.', 'OUTLINED_FUNCTION'))]
        if names_here:
            candidates[words[1], tail].append((address, min(names_here), helper))
    qualified = []
    for address in sorted(missing & old.keys()):
        code = old[address]
        if len(code) != 12:
            continue
        words = struct.unpack('<3I', code)
        calls, refs, _ = scan(code, address)
        if words[0] != 0xd503245f or words[1] & 0xfff0001f != 0xd5300000 or refs or len(calls) != 1 or calls[0][0] != address + 8 or calls[0][2]:
            continue
        pair = pairing['functions'].get(hex(calls[0][1]))
        if not pair or pair['how'] != 'reviewed':
            continue
        matches = [row for row in candidates[words[1], int(pair['native'], 16)]
                   if incoming[address] - {(1, 0, 0, 1)} <= typed[row[0]]]
        if not matches:
            continue
        target, name, helper = min(matches, key=lambda row: row[1])
        qualified.append({'original': hex(address), 'name': name, 'final_import_only': True,
                          'evidence': 'Complete stock BTI/MRS-x0/tail body equals actual pure BTI/MRS-x0/RET helper plus exact native balanced FP/LR/PAC-return-check wrapper; identical concrete sysreg and independently reviewed tail callee; no field-offset normalization',
                          'sysreg_instruction': hex(words[1]), 'tail_pair': pair,
                          'original_sha256': hashlib.sha256(code).hexdigest(), 'native_sha256': hashlib.sha256(new[target]).hexdigest(),
                          'accessor_sha256': hashlib.sha256(new[helper]).hexdigest(),
                          'alternatives': [{'name': row[1], 'address': hex(row[0])} for row in matches]})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'build': '22G100', 'arch': 'arm64e',
                                   'stock_sha256': hashlib.sha256(args.stock_kernel.read_bytes()).hexdigest(),
                                   'native_sha256': hashlib.sha256(args.native_kernel.read_bytes()).hexdigest(),
                                   'functions': qualified, 'data': [], 'bootable': False}, indent=2) + '\n')
    print('Complete pure-accessor/tail wrappers qualified', len(qualified), '; final imports only')


if __name__ == '__main__':
    main()
