#!/usr/bin/env python3
"""Name globals through complete paired straight-line getter/tail wrappers.

Interprets only BTI, register moves, concrete ADRP/ADD, one scalar global
load and RET/already-reviewed tail branch. Complete arguments/return/read
width agree; folded page loads and explicit ADD may differ. No anonymous
adjacency, vote majority, field offset relaxation or inline-call guessing.
"""
import argparse
from collections import defaultdict
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import struct
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'pairing', 'imports', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--native-build', type=Path,
                        help='Actual completed kernel build, required for joint UPL source/global contract')
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain old evidence; choose a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, signed
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    pairing = json.loads(args.pairing.read_text())
    if pairing.get('build') != '22G100' or Path(pairing['original']).resolve() != args.stock_kernel.resolve() or Path(pairing['native']).resolve() != args.native_kernel.resolve():
        parser.error('Require actual-input fresh pairing')
    imports = json.loads(args.imports.read_text())
    missing = {int(key, 16) for rows in imports['missing'].values() for key in rows}
    symbols = native.symbols()
    names = defaultdict(set)
    for address, rows in symbols.items():
        for name in rows:
            names[name].add(address)
    pairs = {int(key, 16): int(row['native'], 16) for key, row in pairing['functions'].items()
             if row.get('how') in ('reviewed', 'vtable') or
             (row.get('semantic_identity') is True and row.get('how', '').startswith('qualified-'))}
    def bodies(image):
        starts = sorted(a for a in image.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in image.segments))
        return {a: image.read(a, b - a) for a, b in zip(starts, starts[1:]) if 12 <= b - a <= 64}
    old, new = bodies(original), bodies(native)
    joint_source_contract = None
    if args.native_build:
        built = json.loads((args.native_build / 'host-core.json').read_text())
        if built.get('returncode') != 0 or not any(Path(row['path']).name == args.native_kernel.name and row['sha256'] == hashlib.sha256(args.native_kernel.read_bytes()).hexdigest() for row in built['artifacts']):
            parser.error('Joint source contract requires the actual completed native build')
        source_file = args.native_build / 'tree/.cache/xnu-work/bsd/kern/ubc_subr.c'
        text = source_file.read_text()
        contract = r'\bubc_upl_map\s*\([^)]*\)\s*\{\s*return\s+vm_upl_map\s*\(\s*kernel_map\s*,\s*upl\s*,\s*dst_addr\s*\)\s*;\s*\}'
        if len(re.findall(contract, text)) != 1:
            raise RuntimeError('Actual compiled UPL source no longer declares the reviewed kernel_map wrapper')
        joint_source_contract = {'file': 'bsd/kern/ubc_subr.c', 'sha256': hashlib.sha256(source_file.read_bytes()).hexdigest(),
                                 'function': '_ubc_upl_map', 'global': '_kernel_map', 'callee': '_vm_upl_map'}
    def effects(image, address, code):
        registers = {i: ('input', i) for i in range(31)}
        registers[31] = ('zero',)
        loads, terminal = [], None
        words = struct.unpack('<' + 'I' * (len(code) // 4), code)
        for index, word in enumerate(words):
            destination, base = word & 31, (word >> 5) & 31
            if word == 0xd65f03c0 and index == len(words) - 1:
                terminal = ('return', registers[0])
                continue
            if word & 0xfc000000 == 0x14000000 and index == len(words) - 1:
                target = address + index * 4 + signed(word & 0x3ffffff, 26) * 4
                terminal = ('tail', target, tuple(registers[i] for i in range(8)))
                continue
            if word in (0xd503245f, 0xd503241f, 0xd503249f, 0xd50324df):
                continue
            if destination >= 18:
                return None
            if word & 0xffe0ffe0 == 0xaa0003e0:
                registers[destination] = registers[(word >> 16) & 31]
            elif word & 0x9f000000 == 0x90000000:
                immediate = signed((((word >> 5) & 0x7ffff) << 2) | ((word >> 29) & 3), 21)
                registers[destination] = ('address', ((address + index * 4) & ~4095) + (immediate << 12))
            elif word & 0xffc00000 == 0x91000000:
                if base == 31 or registers[base][0] != 'address':
                    return None
                registers[destination] = ('address', registers[base][1] + ((word >> 10) & 0xfff))
            elif word & 0xffc00000 in (0xb9400000, 0xf9400000):
                if base == 31 or registers[base][0] != 'address':
                    return None
                width = 64 if word >> 30 == 3 else 32
                slot = registers[base][1] + ((word >> 10) & 0xfff) * (width // 8)
                section = image.section(slot)
                if not section or section['segment'] not in ('__DATA', '__DATA_CONST', '__AUTH_CONST'):
                    return None
                loads.append((slot, width))
                registers[destination] = ('global-value', width)
            else:
                return None
        if len(loads) != 1 or terminal is None:
            return None
        return terminal, loads[0]
    proposals = defaultdict(list)
    function_rows = []
    for first, target in pairs.items():
        if first not in old or target not in new:
            continue
        left, right = effects(original, first, old[first]), effects(native, target, new[target])
        if not left or not right or left[1][1] != right[1][1] or left[0][0] != right[0][0]:
            continue
        lterm, rterm = left[0], right[0]
        if lterm[0] == 'tail':
            if pairs.get(lterm[1]) != rterm[1] or lterm[2] != rterm[2]:
                continue
        elif lterm != rterm:
            continue
        original_slot, native_slot = left[1][0], right[1][0]
        if original_slot not in missing:
            continue
        defined = [name for name in symbols.get(native_slot, []) if len(names[name]) == 1]
        if not defined:
            continue
        proposals[original_slot].append({'native': native_slot, 'name': min(defined),
                                        'paired_wrapper': pairing['functions'][hex(first)]['name'],
                                        'original_code_sha256': hashlib.sha256(old[first]).hexdigest(),
                                        'native_code_sha256': hashlib.sha256(new[target]).hexdigest(),
                                        'load_bits': left[1][1], 'concrete_effect': lterm})
    # An unresolved wrapper can jointly identify itself and its global when
    # the complete argument transformation and independently reviewed tail
    # callee select only one concrete source-global value among ALL source
    # wrappers. Include already matched functions; do not manufacture false
    # uniqueness by removing them from the candidate pool.
    native_effects = [(address, result) for address, code in new.items()
                      if (result := effects(native, address, code))]
    old_pac, new_pac = defaultdict(set), defaultdict(set)
    for pointer in cache.fixups().values():
        if pointer[1]:
            old_pac[pointer[0]].add(pointer[1:])
    for pointer in native.fixups().values():
        if pointer[1]:
            new_pac[pointer[0]].add(pointer[1:])
    for first in sorted(missing & old.keys()):
        if not joint_source_contract:
            break
        left = effects(original, first, old[first])
        if not left or left[0][0] != 'tail' or left[0][1] not in pairs:
            continue
        tail = pairs[left[0][1]]
        matches = []
        for address, right in native_effects:
            if right[0][0] != 'tail' or right[0][1] != tail or right[0][2] != left[0][2] or right[1][1] != left[1][1]:
                continue
            if not old_pac[first] - {(1, 0, 0, 1)} <= new_pac[address]:
                continue
            function_names = [name for name in symbols.get(address, []) if len(names[name]) == 1 and not any(part in name for part in ('.cold.', '.llvm.', 'OUTLINED_FUNCTION'))]
            global_names = [name for name in symbols.get(right[1][0], []) if len(names[name]) == 1]
            matches.append((address, right, min(function_names) if function_names else None,
                            min(global_names) if global_names else None))
        if not matches or len({row[1][1][0] for row in matches}) != 1:
            continue
        named_matches = [row for row in matches if row[2] and row[3]]
        if not named_matches:
            continue
        target, right, function_name, global_name = min(named_matches, key=lambda row: row[2])
        if function_name != joint_source_contract['function'] or global_name != joint_source_contract['global'] or tail not in names[joint_source_contract['callee']]:
            continue
        original_slot, native_slot = left[1][0], right[1][0]
        existing = pairing['data'].get(hex(original_slot))
        if existing and int(existing['native'], 16) != native_slot:
            raise RuntimeError('Joint complete wrapper contradicts existing global identity')
        if original_slot in missing:
            proposals[original_slot].append({'native': native_slot, 'name': global_name,
                                            'paired_wrapper': function_name + ' (joint complete unique-tail contract)',
                                            'original_code_sha256': hashlib.sha256(old[first]).hexdigest(),
                                            'native_code_sha256': hashlib.sha256(new[target]).hexdigest(),
                                            'load_bits': left[1][1], 'concrete_effect': left[0]})
        function_rows.append({'original': hex(first), 'name': function_name, 'final_import_only': True,
                              'evidence': 'Complete straight-line argument/global-read/tail effects with independently reviewed tail callee select one concrete named source global among all source wrappers; all equivalent alternatives read that same actual global; no recursive wrapper substitution or argument-arity normalization',
                              'original_sha256': hashlib.sha256(old[first]).hexdigest(),
                              'native_sha256': hashlib.sha256(new[target]).hexdigest(),
                              'original_global': hex(original_slot), 'native_global': hex(native_slot),
                              'tail_target': hex(tail), 'concrete_effect': left[0],
                              'compiled_source_contract': joint_source_contract,
                              'alternatives': [{'address': hex(row[0]), 'name': row[2]} for row in matches]})
    qualified = []
    for address, rows in proposals.items():
        if len({row['native'] for row in rows}) != 1:
            raise RuntimeError('Complete wrapper evidence contradicts global identity')
        first = rows[0]
        qualified.append({'original': hex(address), 'name': first['name'],
                          'evidence': 'Complete independently reviewed paired straight-line getter/tail wrapper has identical one-global read width and argument/return effects; tail callee independently reviewed; folded ADRP load and explicit ADD preserve concrete address; real unique named source slot, no adjacent globals inferred',
                          'support': rows})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'build': '22G100', 'arch': 'arm64e',
                                   'stock_sha256': hashlib.sha256(args.stock_kernel.read_bytes()).hexdigest(),
                                   'native_sha256': hashlib.sha256(args.native_kernel.read_bytes()).hexdigest(),
                                   'observed_at': datetime.now().astimezone().isoformat(),
                                   'functions': function_rows, 'data': qualified, 'bootable': False}, indent=2) + '\n')
    print('Complete global wrapper contracts qualified', len(qualified), 'data /', len(function_rows), 'final imports; metadata only')


if __name__ == '__main__':
    main()
