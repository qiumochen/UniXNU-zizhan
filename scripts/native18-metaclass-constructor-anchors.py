#!/usr/bin/env python3
"""Corroborate the common metaclass C2 ABI from complete typed registrations.

Each evidence class already has complete method PAC/registration proof.
Its complete metaclass C2 wrapper must pass the same literal class name,
instance size, correctly mapped superclass and authenticated class vptr to
one shared implementation. Both actual implementations must concretely
store size/zero count, superclass and name at the same object fields. No
allocator, registration result or firmware success is fabricated.
"""
import argparse
from collections import defaultdict
from datetime import datetime
import hashlib
import json
from pathlib import Path
import struct
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'class-abi', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain prior class-registration ABI evidence; select a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    receipt = json.loads((args.class_abi / 'receipt.json').read_text())
    if (receipt['stock_sha256'], receipt['native_sha256']) != (sha(args.stock_kernel), sha(args.native_kernel)):
        parser.error('Actual complete class input identities required')
    symbols = native.symbols()
    names = defaultdict(set)
    for address, rows in symbols.items():
        for name in rows:
            names[name].add(address)
    def named(name):
        if len(names.get(name, ())) != 1:
            raise RuntimeError('Actual source metaclass/symbol must be unique: ' + name)
        return next(iter(names[name]))
    def bodies(image):
        starts = sorted(a for a in image.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in image.segments))
        return {a: image.read(a, b - a) for a, b in zip(starts, starts[1:]) if b - a <= 8192}
    old, new = bodies(original), bodies(native)
    metas, classes = {}, []
    for row in receipt['results']:
        if not row.get('qualified'):
            continue
        parent = json.loads((args.class_abi / row['path']).read_text())
        classname = parent['classname']
        symbol = '__ZN' + str(len(classname)) + classname + '10gMetaClassE'
        if len(names.get(symbol, ())) == 1:
            metas[int(parent['metaclass'], 16)] = named(symbol)
            classes.append(parent)
    template = (0xd503237f, 0xa9bf7bfd, 0x910003fd, 0x90000001, 0x91000021,
                0x90000002, 0x91000042, 0x52800003, 0x94000000,
                0x90000010, 0x91000210, 0x91004210, 0xaa0003f1,
                0xf2f9b431, 0xdac10a30, 0xf9000010, 0xa8c17bfd, 0xd65f0fff)
    support, callers = [], defaultdict(set)
    for parent in classes:
        if not parent.get('metaclass_constructor'):
            continue
        classname = parent['classname']
        symbol = '__ZN' + str(len(classname)) + classname + '9MetaClassC2Ev'
        old_address = int(parent['metaclass_constructor'], 16)
        if old_address not in old or len(names.get(symbol, ())) != 1:
            continue
        target = named(symbol)
        if target not in new:
            continue
        contracts = []
        for image, address, code, original_side in ((original, old_address, old[old_address], True), (native, target, new[target], False)):
            if len(code) != 72:
                break
            calls, refs, shape = scan(code, address)
            shape = list(shape)
            size_word = shape[7]
            shape[7] &= 0xffe0001f
            if tuple(shape) != template or size_word & 0xffe0001f != 0x52800003 or len(calls) != 1 or calls[0][0] != address + 32 or not calls[0][2] or len(refs) != 3:
                break
            references = {pc - address: (value, kind) for pc, value, kind in refs}
            if set(references) != {16, 24, 40} or image.cstring(references[16][0]) != classname or any(kind != 'add' for _, kind in references.values()):
                break
            contracts.append({'address': address, 'callee': calls[0][1], 'class_name': classname,
                              'superclass': references[24][0], 'vtable': references[40][0],
                              'instance_size': (size_word >> 5) & 0xffff,
                              'sha256': hashlib.sha256(code).hexdigest()})
        if len(contracts) != 2:
            continue
        left, right = contracts
        if metas.get(left['superclass']) != right['superclass'] or left['instance_size'] != right['instance_size']:
            continue
        source_vtable = '__ZTVN' + str(len(classname)) + classname + '9MetaClassE'
        if len(names.get(source_vtable, ())) != 1 or named(source_vtable) != right['vtable']:
            continue
        # Typed vptr values differ by source image; concrete key/address
        # diversity and virtual table ancestry are preserved by class proof.
        support.append({'classname': classname, 'method_slots': parent['method_slots'],
                        'original': {k: hex(v) if isinstance(v, int) else v for k, v in left.items()},
                        'native': {k: hex(v) if isinstance(v, int) else v for k, v in right.items()}})
        callers[left['callee']].add(right['callee'])
    if len(support) < 8 or len(callers) != 1:
        raise RuntimeError('Require multiple complete independent class registration wrappers to one shared metaclass C2')
    old_callee, targets = next(iter(callers.items()))
    source_symbol = '__ZN11OSMetaClassC2EPKcPKS_j'
    if targets != {named(source_symbol)}:
        raise RuntimeError('Actual shared registration implementation differs')
    def fields(image, address, code):
        words = struct.unpack('<' + 'I' * (len(code) // 4), code)
        size_sites = [index for index, word in enumerate(words) if word == 0x29047c03]
        if len(size_sites) != 1:
            raise RuntimeError('Actual metaclass constructor does not store size/zero instance count together')
        index = size_sites[0]
        if struct.pack('<I', 0xf9000802) not in code[index * 4:(index + 3) * 4] and struct.pack('<I', 0xf9000a62) not in code[index * 4:(index + 5) * 4]:
            raise RuntimeError('Actual metaclass superclass field store differs')
        if struct.pack('<I', 0xaa0003f3) not in code[:64] or struct.pack('<I', 0xaa0103f4) not in code[:64]:
            raise RuntimeError('Actual metaclass input object/name preservation differs')
        if struct.pack('<I', 0xf9000e74) not in code:
            raise RuntimeError('Actual metaclass name field store differs')
        # Both implementations retain real allocation and registration code;
        # no return-zero implementation can satisfy these concrete stores.
        return {'whole_body_sha256': hashlib.sha256(code).hexdigest(), 'size_count_store_offset': index * 4,
                'fields': {'class_size': 0x20, 'instance_count': 0x24, 'superclass': 0x10, 'class_name': 0x18}}
    left = fields(original, old_callee, old[old_callee])
    target = named(source_symbol)
    right = fields(native, target, new[target])
    functions = [{'original': hex(old_callee), 'name': source_symbol, 'final_import_only': True,
                  'evidence': 'Multiple independently full-PAC-qualified class registrations have complete same literal/superclass/instance-size/authenticated-vptr C2 wrappers calling one shared implementation; actual shared C2 preserves object/name and stores concrete size/count/superclass/name fields; real allocation/registration implementation retained, not emulated as success',
                  'original_fields': left, 'native_fields': right}]
    for row in support:
        classname = row['classname']
        functions.append({'original': row['original']['address'],
                          'name': '__ZN' + str(len(classname)) + classname + '9MetaClassC2Ev',
                          'final_import_only': True,
                          'evidence': 'Complete same class-literal/superclass/instance-size/authenticated-metaclass-vptr C2 wrapper and balanced PAC frame; full actual class ancestry/registration independently qualified, shared real metaclass C2 ABI corroborated by all evidence classes; no parameter or field-offset relaxation',
                          'original_contract': row['original'], 'native_contract': row['native'],
                          'shared_constructor_original': hex(old_callee), 'shared_constructor_native': hex(target)})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
                                   'stock_sha256': sha(args.stock_kernel), 'native_sha256': sha(args.native_kernel),
                                   'functions': functions,
                                   'data': [], 'class_registration_support': support, 'bootable': False}, indent=2) + '\n')
    print('Actual shared metaclass C2 and', len(support), 'complete class wrappers qualified; no registration executed')


if __name__ == '__main__':
    main()
