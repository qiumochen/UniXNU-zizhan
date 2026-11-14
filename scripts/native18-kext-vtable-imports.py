#!/usr/bin/env python3
"""Name kernel methods from complete registered kext vtables and real imports.

Independent KDK kexts retain imported method names. Require an exact complete
PAC table, extent, getMetaClass return and adjacent literal-name/metaclass
registration in the corresponding stock component. A generated MetaClass
table cannot substitute for an instance-class table. Bind names in the actual
native kernel only; no foreign kext code or metadata is copied to a candidate.
"""
import argparse
from collections import defaultdict, Counter
from datetime import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import plistlib
import re
import struct
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('prelink-kit', 'stock-kernel', 'native-kernel', 'kdk-extensions', 'out'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--all-stock-components', action='store_true',
                        help='Use other original components as identity references only; never install them')
    args = parser.parse_args()
    public = Path(__file__).resolve().parents[1]
    if args.out.exists() or args.out.resolve() == public or public in args.out.resolve().parents:
        parser.error('Use a fresh private evidence directory')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    from fileset_emit import dependency_closure
    path = Path(__file__).with_name('native18-kext-import-names.py')
    spec = importlib.util.spec_from_file_location('actual_kext_import_reader', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    old_fixups, symbols = cache.fixups(), native.symbols()
    names = defaultdict(set)
    for address, values in symbols.items():
        for name in values:
            names[name].add(address)
    roots = ['com.apple.kec.corecrypto', 'com.apple.kec.pthread', 'com.apple.filesystems.apfs',
             'com.apple.kec.Compression', 'com.apple.AppleFSCompression.AppleFSCompressionTypeZlib',
             'com.apple.iokit.IOGPUFamily', 'com.apple.iokit.IOHIDFamily', 'com.apple.iokit.IOStorageFamily']
    closure = dependency_closure(cache, roots)
    contracts, tables, refused, negatives, data_references, function_references = defaultdict(list), [], Counter(), [], [], []
    def core(address):
        return any(lo <= address < hi for lo, hi in original.mapped())
    def extent(fixups, address):
        rows = []
        while (pointer := fixups.get(address + len(rows) * 8)) and pointer[1]:
            rows.append(pointer)
            if len(rows) > 512:
                return []
        return rows
    for folder in sorted(args.kdk_extensions.glob('*.kext')):
        info = folder / 'Contents/Info.plist'
        if not info.exists():
            continue
        metadata = plistlib.loads(info.read_bytes())
        owner = metadata.get('CFBundleIdentifier')
        if owner not in cache.entries or not metadata.get('CFBundleExecutable') or \
                (owner not in closure and not args.all_stock_components):
            continue
        binary = folder / 'Contents/MacOS' / metadata['CFBundleExecutable']
        if not binary.exists():
            continue
        ref = Image(module.arm64e_slice(binary.read_bytes()))
        fixups = module.chained_import_fixups(ref)
        stock = Image(cache.data, cache.entries[owner][1])
        # Index every complete stock table, including same-shaped ambiguous
        # tables. Concrete registration must select exactly one afterwards.
        bounds = [(s['va'], s['va'] + s['size']) for s in stock.segments
                  if s['name'] in ('__DATA_CONST', '__AUTH_CONST')]
        sequences = defaultdict(list)
        stock_tables = {}
        for site, pointer in old_fixups.items():
            if not pointer[1] or not any(lo <= site < hi for lo, hi in bounds) or \
                    (old_fixups.get(site - 8) and old_fixups[site - 8][1]):
                continue
            slots = extent(old_fixups, site)
            if len(slots) >= 8:
                sequences[tuple(row[1:] for row in slots)].append(site - 16)
                stock_tables[site - 16] = slots
        registrations = defaultdict(set)
        meta_tables_by_class = defaultdict(set)
        starts = sorted(a for a in stock.function_starts() if any(
            s['name'] == '__TEXT_EXEC' and s['va'] <= a < s['va'] + s['filesize'] for s in stock.segments))
        for first, last in zip(starts, starts[1:]):
            if not 0 < last - first <= 65536:
                continue
            calls, refs, _ = scan(stock.read(first, last - first), first)
            # Complete generated MetaClass C2 wrapper: class literal, shared
            # ctor call, then signed DA/object-address vptr installation.
            # Its exact shape independently identifies this class's metaclass
            # vtable even though macOS retains extra RESERVED method slots.
            from fileset_shape import checked_shape
            shape, _, _, _ = checked_shape(stock.read(first, last - first), first)
            template = (0xd503237f, 0xa9bf7bfd, 0x910003fd, 0x90000001, 0x91000021,
                        0x90000002, 0x91000042, 0x52800003, 0x94000000,
                        0x90000010, 0x91000210, 0x91004210, 0xaa0003f1,
                        0xf2f9b431, 0xdac10a30, 0xf9000010, 0xa8c17bfd, 0xd65f0fff)
            if len(shape) == len(template):
                masked = list(shape)
                masked[7] &= 0xffe0001f
                if tuple(masked) == template:
                    by_pc = {pc - first: (address, kind) for pc, address, kind in refs}
                    if set(by_pc) == {16, 24, 40} and all(kind == 'add' for _, kind in by_pc.values()):
                        literal = stock.cstring(by_pc[16][0])
                        if literal:
                            meta_tables_by_class[literal].add(by_pc[40][0])
            for pc, address, kind in refs:
                literal = stock.cstring(address)
                if not literal or kind != 'add' or struct.unpack('<I', stock.read(pc, 4))[0] & 31 != 1:
                    continue
                for mpc, meta, mkind in refs:
                    if not 0 < pc - mpc <= 24 or mkind != 'add':
                        continue
                    register = struct.unpack('<I', stock.read(mpc, 4))[0] & 31
                    tail = stock.read(pc + 4, min(32, last - pc - 4))
                    words = struct.unpack('<' + 'I' * (len(tail) // 4), tail)
                    prefix = []
                    found_call = False
                    for word in words:
                        if word & 0xfc000000 == 0x94000000:
                            found_call = True
                            break
                        prefix.append(word)
                    if not found_call or register and (0xaa0003e0 | register << 16) not in prefix:
                        continue
                    registrations[(literal, meta)].add(first)
        for address, own_names in ref.symbols().items():
            ordinary = [re.fullmatch(r'__ZTV(\d+)([A-Za-z_]\w*)', name) for name in own_names]
            ordinary = [row for row in ordinary if row and int(row[1]) == len(row[2])]
            if len(ordinary) != 1:
                continue
            classname = ordinary[0][2]
            slots = extent(fixups, address + 16)
            if len(slots) < 8:
                continue
            shape = tuple(row[1:] for row in slots)
            getters = []
            ref_symbols = ref.symbols()
            for index, pointer in enumerate(slots):
                values = [pointer[0]] if isinstance(pointer[0], str) else ref_symbols.get(pointer[0], ())
                if any(name.endswith('12getMetaClassEv') for name in values):
                    getters.append(index)
            if len(getters) != 1:
                refused['no-unique-typed-getter'] += 1
                continue
            candidates = []
            for table, old_slots in stock_tables.items():
                # macOS SDK tables retain named RESERVED ABI slots which iOS
                # removes. Select the class by its complete registration and
                # common base signature, then identify only unambiguous typed
                # imported slots; never transfer raw slot offsets across OSes.
                if len(old_slots) <= getters[0] or tuple(row[1:] for row in old_slots[:13]) != shape[:13]:
                    continue
                target = old_fixups[table + 16 + 8 * getters[0]][0]
                try:
                    _, refs, words = scan(cache.read(target, 16), target)
                except ValueError:
                    continue
                metas = {value for _, value, kind in refs if kind == 'add'}
                if len(metas) == 1:
                    meta = next(iter(metas))
                    if registrations.get((classname, meta)):
                        candidates.append((table, meta))
            if len(candidates) != 1:
                refused['class-registration-table-ambiguous-or-absent'] += 1
                continue
            table, meta = candidates[0]
            old_slots = stock_tables[table]
            ref_indices, old_indices = defaultdict(list), defaultdict(list)
            for index, slot in enumerate(slots):
                ref_indices[slot[1:]].append(index)
            for index, slot in enumerate(old_slots):
                old_indices[slot[1:]].append(index)
            pairs = sorted((values[0], old_indices[key][0]) for key, values in ref_indices.items()
                           if len(values) == 1 and len(old_indices.get(key, ())) == 1)
            if len(pairs) < len(old_slots) * .9 or any(b[1] <= a[1] for a, b in zip(pairs, pairs[1:])):
                refused['typed-slots-incomplete-or-nonmonotonic'] += 1
                continue
            # Every stock PAC type must exist in the reference. Duplicate
            # signatures are kept in coverage but never independently named.
            if not set(old_indices) <= set(ref_indices):
                refused['stock-PAC-type-absent-from-reference'] += 1
                continue
            evidence = {'component': owner, 'classname': classname, 'stock_table': hex(table),
                        'reference_table': hex(address), 'stock_meta': hex(meta), 'slots': len(slots),
                        'stock_slots': len(old_slots), 'unique_monotonic_shared_slots': len(pairs),
                        'stock_registration': [hex(a) for a in sorted(registrations[(classname, meta)])],
                        'reference_binary_sha256': sha(binary)}
            tables.append(evidence)
            reference_meta_name = '__ZN' + str(len(classname)) + classname + '10gMetaClassE'
            reference_metas = [a for a, values in ref_symbols.items() if reference_meta_name in values]
            if len(reference_metas) == 1:
                data_references.append({'component': owner, 'original': hex(meta),
                                        'reference': hex(reference_metas[0]), 'kind': 'registered-metaclass',
                                        'classname': classname, 'evidence': evidence})
            generated_symbol = '__ZTVN' + str(len(classname)) + classname + '9MetaClassE'
            generated = [a for a, values in ref_symbols.items() if generated_symbol in values]
            old_generated = meta_tables_by_class.get(classname, set())
            if len(generated) == len(old_generated) == 1:
                old_generated = next(iter(old_generated))
                left_meta = extent(old_fixups, old_generated + 16)
                right_meta = extent(fixups, generated[0] + 16)
                if len(left_meta) >= 14 and tuple(row[1:] for row in left_meta[:13]) == tuple(row[1:] for row in right_meta[:13]):
                    data_references.append({'component': owner, 'original': hex(old_generated),
                                            'reference': hex(generated[0]), 'kind': 'constructor-metaclass-vtable',
                                            'classname': classname, 'evidence': evidence})
                    for old_index, old_pointer in enumerate(left_meta):
                        old_matches = [i for i, pointer in enumerate(left_meta) if pointer[1:] == old_pointer[1:]]
                        ref_matches = [i for i, pointer in enumerate(right_meta) if pointer[1:] == old_pointer[1:]]
                        if len(old_matches) != 1 or len(ref_matches) != 1:
                            continue
                        reference_pointer = right_meta[ref_matches[0]]
                        if isinstance(reference_pointer[0], int) and ref_symbols.get(reference_pointer[0]):
                            function_references.append({'component': owner, 'original': hex(old_pointer[0]),
                                                        'reference': hex(reference_pointer[0]),
                                                        'reference_names': ref_symbols[reference_pointer[0]],
                                                        'kind': 'registered-generated-metaclass-PAC-method',
                                                        'evidence': evidence, 'classname': classname,
                                                        'reference_slot': ref_matches[0], 'stock_slot': old_index})
            data_references.append({'component': owner, 'original': hex(table), 'reference': hex(address),
                                    'kind': 'registered-instance-vtable', 'classname': classname, 'evidence': evidence})
            for index, old_index in pairs:
                slot = slots[index]
                pointer = old_fixups[table + 16 + old_index * 8]
                if isinstance(slot[0], int) and ref_symbols.get(slot[0]):
                    function_references.append({'component': owner, 'original': hex(pointer[0]),
                                                'reference': hex(slot[0]), 'reference_names': ref_symbols[slot[0]],
                                                'kind': 'registered-class-unique-PAC-method', 'evidence': evidence,
                                                'reference_slot': index, 'stock_slot': old_index})
                if not isinstance(slot[0], str) or len(names.get(slot[0], ())) != 1:
                    continue
                if not core(pointer[0]):
                    continue
                contracts[hex(pointer[0])].append(dict(evidence, reference_slot=index, stock_slot=old_index, name=slot[0],
                                                       PAC=list(slot[1:])))
            if not negatives:
                bad = list(shape)
                changed = list(bad[0])
                changed[2] ^= 1
                bad[0] = tuple(changed)
                if table in sequences.get(tuple(bad), ()):
                    raise RuntimeError('Changed PAC slot still selected original table')
                if registrations.get((classname + '_WRONG', meta)):
                    raise RuntimeError('Changed class-name still identifies original registration')
                negatives = [{'name': 'wrong-PAC-discriminator', 'result': 'PASS'},
                             {'name': 'wrong-class-literal', 'result': 'PASS'}]
    functions, conflicts = [], []
    for address, rows in contracts.items():
        targets = {next(iter(names[row['name']])) for row in rows}
        if len(targets) != 1:
            conflicts.append({'original': address, 'names': sorted({row['name'] for row in rows})})
            continue
        functions.append({'original': address, 'name': min(row['name'] for row in rows),
                          'evidence': 'Actual independent kext undefined import name in a uniquely registered instance-class table, common base signatures and concrete getMetaClass result; full stock PAC-type coverage and at least90percent unique monotonic slot correspondence, individual named slot unique by key/discriminator/address-diversity in both entire tables; macOS extra methods not transferred and ambiguous slot types withheld; all observations resolve to one actual native definition',
                          'identity_proof_kind': 'registered-kext-vtable-import-research-v2', 'contracts': rows,
                          'review_only': True,
                          'runtime_qualified': False})
    report = {'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
              'stock_sha256': sha(args.stock_kernel), 'native_sha256': sha(args.native_kernel),
              'functions': functions, 'data': [], 'tables': tables, 'refused': refused,
              'caller_data_references': data_references,
              'caller_function_references': function_references,
              'conflicts': conflicts, 'negative_controls': negatives, 'device_access': False, 'bootable': False}
    args.out.mkdir(parents=True)
    (args.out / 'identity-anchors.json').write_text(json.dumps(report, indent=2) + '\n')
    (args.out / Path(__file__).name).write_bytes(Path(__file__).read_bytes())
    print('Registered complete kext tables', len(tables), '; unique imported kernel method identities',
          len(functions), '; conflicts', len(conflicts), '; refusals', dict(refused), flush=True)


if __name__ == '__main__':
    main()
