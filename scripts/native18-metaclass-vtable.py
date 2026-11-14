#!/usr/bin/env python3
"""Identify OSMetaClass's own table through the verified constructor vptr.

Generated MetaClass subclasses share complete discriminator tables and the
same getMetaClass result. Class-name registration therefore cannot select the
base OSMetaClass table. Its independently corroborated C2 writes that exact
table before the first call. Verify this concrete write and every actual
compiled-object/native/stock slot; keep all other class evidence unchanged.
"""
import argparse
from datetime import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('native-source', 'prelink-kit', 'stock-kernel', 'native-kernel',
                'compiled-object', 'constructor-proof', 'class-abi', 'out'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain prior evidence; select a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    blob = args.stock_kernel.read_bytes()
    cache, native = Image(blob), Image(args.native_kernel.read_bytes())
    original = Image(blob, cache.entries['com.apple.kernel'][1])
    hashes = (sha(args.stock_kernel), sha(args.native_kernel))
    proof = json.loads(args.constructor_proof.read_text())
    abi = json.loads((args.class_abi / 'receipt.json').read_text())
    if any((row.get('stock_sha256'), row.get('native_sha256')) != hashes for row in (proof, abi)):
        parser.error('Actual constructor/class evidence belongs to different kernels')
    supports = proof.get('class_registration_support', [])
    if len(supports) < 8 or len({r['classname'] for r in supports}) != len(supports):
        parser.error('Need independently corroborated typed constructor registrations')
    symbol = '__ZN11OSMetaClassC2EPKcPKS_j'
    rows = [row for row in proof['functions'] if row['name'] == symbol]
    if len(rows) != 1:
        parser.error('Need the actual shared C2 contract')
    contract = rows[0]
    symbols = native.symbols()
    def named(name):
        values = [address for address, names in symbols.items() if name in names]
        if len(values) != 1:
            raise RuntimeError('Actual native symbol is not unique: ' + name)
        return values[0]
    old_c2, new_c2 = int(contract['original'], 16), named(symbol)
    body_ends = {}
    def full_body(image, address):
        if id(image) not in body_ends:
            starts = sorted(x for x in image.function_starts() if any(
                s['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and
                s['va'] <= x < s['va'] + s['filesize'] for s in image.segments))
            body_ends[id(image)] = dict(zip(starts, starts[1:]))
        return image.read(address, body_ends[id(image)][address] - address)
    old_code, new_code = full_body(original, old_c2), full_body(native, new_c2)
    for code, key in ((old_code, 'original_fields'), (new_code, 'native_fields')):
        if hashlib.sha256(code).hexdigest() != contract[key]['whole_body_sha256']:
            parser.error('Constructor code no longer matches independently checked contract')
    def vptr(code, start):
        calls, refs, shape = scan(code, start)
        first_call = min((pc for pc, _, link in calls if link), default=start + len(code))
        # Only the exact DA-address-diversified base-object write, before
        # registration/allocation calls, may identify the base table.
        pattern = (0x90000010, 0x91000210, 0x91004210, 0xaa0003f1,
                   0xf2f9b431, 0xdac10a30, 0xf9000010)
        hits = []
        for index in range(len(shape) - len(pattern) + 1):
            if tuple(shape[index:index + len(pattern)]) != pattern or start + (index + len(pattern)) * 4 > first_call:
                continue
            values = [value for pc, value, kind in refs if pc == start + (index + 1) * 4 and kind == 'add']
            if len(values) == 1:
                hits.append(values[0])
        if len(hits) != 1:
            raise RuntimeError('Constructor has no unique exact authenticated base vptr write')
        return hits[0]
    old_table, new_table = vptr(old_code, old_c2), vptr(new_code, new_c2)
    if new_table != named('__ZTV11OSMetaClass'):
        raise RuntimeError('Constructor does not write the actual source OSMetaClass table')
    path = args.native_source / 'scripts/derive-pong-fileset-parent.py'
    spec = importlib.util.spec_from_file_location('actual_parent_pac_slots', path)
    module = importlib.util.module_from_spec(spec)
    # Keep original reference-object parser except its storage-family minimum.
    code = path.read_text()
    needle = 'if len(result)<150 or len(result)>512:'
    if code.count(needle) != 1:
        raise RuntimeError('Review changed source-object PAC parser')
    exec(compile(code.replace(needle, 'if len(result)<8 or len(result)>512:'), str(path), 'exec'), module.__dict__)
    slots = module.reference_slots(args.compiled_object, 'OSMetaClass')
    old_fixups, new_fixups = cache.fixups(), native.fixups()
    def validate(table, fixups):
        for index, slot in enumerate(slots):
            pointer = fixups.get(table + 16 + index * 8)
            expected = (1, slot['key'], slot['diversity'], slot['address_diversity'])
            if pointer is None or pointer[1:] != expected:
                raise RuntimeError('Complete OSMetaClass PAC slot differs: ' + str(index))
        following = fixups.get(table + 16 + len(slots) * 8)
        if following and following[1]:
            raise RuntimeError('Matched prefix extends beyond the actual source vtable')
    validate(old_table, old_fixups)
    validate(new_table, new_fixups)
    for index, slot in enumerate(slots):
        address = new_fixups[new_table + 16 + index * 8][0]
        if slot['name'] not in symbols.get(address, ()):
            raise RuntimeError('Compiled-object method and actual source function differ: ' + slot['name'])
    controls = []
    changed = list(struct.unpack('<' + 'I' * (len(new_code) // 4), new_code))
    location = changed.index(0x91004210)
    changed[location] = 0x91006210  # wrong vtable address point: +24 rather than +16
    try:
        vptr(struct.pack('<' + 'I' * len(changed), *changed), new_c2)
    except RuntimeError:
        controls.append({'name': 'wrong-source-address-point', 'result': 'PASS'})
    else:
        raise RuntimeError('Wrong vptr address point accepted')
    corrupt = dict(old_fixups)
    site = old_table + 16
    pointer = list(corrupt[site])
    pointer[3] ^= 1
    corrupt[site] = tuple(pointer)
    try:
        validate(old_table, corrupt)
    except RuntimeError:
        controls.append({'name': 'wrong-slot-discriminator', 'result': 'PASS'})
    else:
        raise RuntimeError('Wrong method discriminator accepted')
    getter_index = next(index for index, row in enumerate(slots) if '12getMetaClassEv' in row['name'])
    getter = old_fixups[old_table + 16 + getter_index * 8][0]
    _, refs, _ = scan(original.read(getter, 16), getter)
    metas = {value for _, value, kind in refs if kind == 'add'}
    if len(metas) != 1:
        raise RuntimeError('Actual OSMetaClass metaclass getter is not a unique pointer')
    parent = {'build': '22G100', 'arch': 'arm64e', 'entry': 'com.apple.kernel', 'classname': 'OSMetaClass',
              'method_slots': len(slots), 'vtable': hex(old_table), 'metaclass': hex(next(iter(metas))),
              'constructor': hex(old_c2), 'abi_compatible': True, 'abi_mismatches': [],
              'runtime_qualified': False, 'stock_sha256': hashes[0], 'native_sha256': hashes[1],
              'compiled_object_sha256': sha(args.compiled_object),
              'evidence': 'Independently corroborated shared typed C2 writes the exact authenticated base OSMetaClass vptr before any call; actual source vtable symbol agrees; complete compiled-object and actual native/stock PAC slots and exact table extent agree',
              'methods': [dict(slot=index, **row, address=hex(old_fixups[old_table + 16 + index * 8][0]))
                          for index, row in enumerate(slots)]}
    args.out.mkdir(parents=True)
    # New overlay receipt; preserve the previous complete evidence untouched.
    for row in abi['results']:
        if row.get('qualified'):
            (args.out / row['path']).symlink_to((args.class_abi / row['path']).resolve())
    (args.out / 'OSMetaClass.json').write_text(json.dumps(parent, indent=2) + '\n')
    # Empty generated MetaClass D1 destructors are either an exact native
    # tail adapter to the base D2 or an inlined copy of that body in stock.
    # Match the latter against ALL actual stock instructions and operands,
    # rather than mistaking the base vtable's deliberate D1 trap for D2.
    from fileset_shape import checked_shape
    def stock_body_key(address):
        raw = full_body(original, address)
        shape, positions, calls, refs = checked_shape(raw, address)
        return (tuple(shape), tuple((positions[pc - address],
                    ('local', positions[target - address]) if address <= target < address + len(raw)
                    else ('external', target), link) for pc, target, link in calls),
                tuple((positions[pc - address], value, kind) for pc, value, kind in refs))
    stock_starts = sorted(address for address in original.function_starts() if any(
        s['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and
        s['va'] <= address < s['va'] + s['filesize'] for s in original.segments))
    destructor = named('__ZN11OSMetaClassD2Ev')
    destructor_support, keys = [], []
    for support in supports:
        classname = support['classname']
        old_meta = int(support['original']['vtable'], 16)
        new_meta = int(support['native']['vtable'], 16)
        validate(old_meta, old_fixups)
        validate(new_meta, new_fixups)
        old_d1 = old_fixups[old_meta + 16][0]
        new_d1 = new_fixups[new_meta + 16][0]
        d1_symbol = '__ZN' + str(len(classname)) + classname + '9MetaClassD1Ev'
        if new_meta != named('__ZTVN' + str(len(classname)) + classname + '9MetaClassE') or new_d1 != named(d1_symbol):
            raise RuntimeError('Actual typed generated metaclass destructor differs')
        raw = full_body(native, new_d1)
        calls, refs, shape = scan(raw, new_d1)
        if shape != [0xd503245f, 0x14000000] or refs or calls != [(new_d1 + 4, destructor, False)]:
            raise RuntimeError('Generated destructor is not an exact balanced tail adapter to base D2')
        keys.append(stock_body_key(old_d1))
        destructor_support.append({'classname': classname, 'original': hex(old_d1),
                                   'native': hex(new_d1), 'native_tail_target': hex(destructor)})
    if len(set(keys)) != 1:
        raise RuntimeError('Typed stock metaclass destructor complete operands disagree')
    key = keys[0]
    matches = []
    for first, last in zip(stock_starts, stock_starts[1:]):
        if 0 < last - first <= 8192 and stock_body_key(first) == key:
            matches.append(first)
    if not matches:
        raise RuntimeError('No complete stock metaclass destructor implementations')
    # A wrong external target must change the key even when its masked opcode
    # remains identical. This directly covers the prior weak-shape failure.
    external = next(index for index, row in enumerate(key[1]) if row[1][0] == 'external')
    mutated = list(key[1])
    pc, (_, target), link = mutated[external]
    mutated[external] = (pc, ('external', target + 4), link)
    if (key[0], tuple(mutated), key[2]) == key:
        raise RuntimeError('Changed destructor callee operand accepted')
    controls.append({'name': 'wrong-complete-destructor-callee', 'result': 'PASS'})
    anchors = {'build': '22G100', 'arch': 'arm64e', 'stock_sha256': hashes[0], 'native_sha256': hashes[1],
               'functions': [{'original': hex(address), 'name': '__ZN11OSMetaClassD2Ev', 'final_import_only': True,
                              'evidence': 'Complete actual stock instructions, internal branches and every call/data operand equal independently constructor-identified full-PAC generated MetaClass D1 implementations; all actual native typed D1 slots are exact BTI/tail adapters to real OSMetaClass D2; base vtable D1 trap is not substituted'}
                             for address in matches], 'data': [], 'support': destructor_support,
               'negative_controls': controls, 'bootable': False, 'device_access': False}
    (args.out / 'metaclass-destructor-anchors.json').write_text(json.dumps(anchors, indent=2) + '\n')
    abi['results'] = [row for row in abi['results'] if row['classname'] != 'OSMetaClass']
    abi['results'].append({'classname': 'OSMetaClass', 'slots': len(slots), 'qualified': True, 'path': 'OSMetaClass.json'})
    abi.update(observed_at=datetime.now().astimezone().isoformat(), qualified=sum(row['qualified'] for row in abi['results']),
               negative_controls=controls, extension='constructor-vptr OSMetaClass base identity',
               constructor_proof_sha256=sha(args.constructor_proof), original_class_abi_receipt_sha256=sha(args.class_abi / 'receipt.json'))
    (args.out / 'receipt.json').write_text(json.dumps(abi, indent=2) + '\n')
    (args.out / Path(__file__).name).write_bytes(Path(__file__).read_bytes())
    print('OSMetaClass complete PAC table:', len(slots), 'slots;', abi['qualified'], 'classes;',
          len(matches), 'complete typed destructor implementations; negative controls', controls)


if __name__ == '__main__':
    main()
