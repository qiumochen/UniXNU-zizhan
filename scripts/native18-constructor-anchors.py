#!/usr/bin/env python3
"""Qualify inlined refcount/vptr constructors against real source call chains.

Requires a complete independently qualified class PAC table/registration,
the exact stock count=1/authenticated-vptr body, unique real C2 source
definition and actual source constructor/base-constructor instruction replay.
Every other byte of the fixture object and every callee-saved register must
remain unchanged. Authentication is abstracted for host object effects only.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import random
import struct
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'class-abi', 'imports', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain prior replay; select a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    from native18_replay import KernelReplay
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    abi = json.loads((args.class_abi / 'receipt.json').read_text())
    if (abi['stock_sha256'], abi['native_sha256']) != (sha(args.stock_kernel), sha(args.native_kernel)):
        parser.error('Actual complete class ABI input hashes required')
    imports = json.loads(args.imports.read_text())
    missing = {int(key, 16) for rows in imports['missing'].values() for key in rows}
    symbols = native.symbols()
    names = {}
    for address, rows in symbols.items():
        for name in rows:
            names.setdefault(name, set()).add(address)
    template = (0xd503245f, 0x52800028, 0xb9000808, 0x90000010, 0x91000210,
                0x91004210, 0xaa0003f1, 0xf2f9b431, 0xdac10a30, 0xf9000010, 0xd65f03c0)
    starts = sorted(a for a in original.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in original.segments))
    candidates = {}
    for address, end in zip(starts, starts[1:]):
        if end - address != 44 or address not in missing:
            continue
        code = original.read(address, 44)
        calls, refs, shape = scan(code, address)
        if tuple(shape) == template and not calls and len(refs) == 1 and refs[0][2] == 'add':
            candidates.setdefault(refs[0][1], []).append(address)
    left, right = KernelReplay(original), KernelReplay(native)
    native_bodies = {}
    native_starts = sorted(a for a in native.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in native.segments))
    for a, b in zip(native_starts, native_starts[1:]):
        native_bodies[a] = b - a
    rng = random.Random(0x22c011)
    qualified, refused = [], []
    count = 0
    for row in abi['results']:
        if not row.get('qualified'):
            continue
        parent = json.loads((args.class_abi / row['path']).read_text())
        if not parent.get('abi_compatible'):
            continue
        classname = row['classname']
        old_vtable = int(parent['vtable'], 16)
        constructor = '__ZN' + str(len(classname)) + classname + 'C2EPK11OSMetaClass'
        vtable_name = '__ZTV' + str(len(classname)) + classname
        if old_vtable not in candidates or len(names.get(constructor, ())) != 1 or len(names.get(vtable_name, ())) != 1:
            continue
        target, new_vtable = next(iter(names[constructor])), next(iter(names[vtable_name]))
        if not 24 <= native_bodies.get(target, 0) <= 256:
            refused.append({'classname': classname, 'reason': 'source constructor body is outside bounded contract'})
            continue
        # Concrete source vptr auth must use the same cda1 object-address
        # discriminator. Actual base constructors remain executed below.
        source_code = native.read(target, native_bodies[target])
        if struct.pack('<3I', 0xaa0003f1, 0xf2f9b431, 0xdac10a30) not in source_code:
            refused.append({'classname': classname, 'reason': 'source constructor vptr authentication shape differs'})
            continue
        for first in candidates[old_vtable]:
            try:
                for _ in range(16):
                    offset = rng.choice((0, 16, 64, 256))
                    address = 0x50b0000 + offset
                    initial = bytes(rng.getrandbits(8) for _ in range(1024))
                    saved = {index: rng.getrandbits(64) for index in range(19, 30)}
                    for machine, entry, table in ((left, first, old_vtable), (right, target, new_vtable)):
                        machine.uc.mem_write(address, initial)
                        result = machine.run(entry, [address, 0x50d0000], saved)
                        actual = bytes(machine.uc.mem_read(address, 1024))
                        expected = bytearray(initial)
                        struct.pack_into('<Q', expected, 0, table + 16)
                        struct.pack_into('<I', expected, 8, 1)
                        if result != address or actual != bytes(expected):
                            raise RuntimeError('Actual constructor/base chain changes unexpected object bytes or return register')
                        if any(machine.uc.reg_read(machine.registers[index]) != saved[index] for index in saved):
                            raise RuntimeError('Actual source constructor does not restore callee-saved registers')
                    count += 1
                qualified.append({'original': hex(first), 'name': constructor, 'final_import_only': True,
                                  'evidence': 'Complete stock count=1/cda1 authenticated class-vptr store matches actual independently qualified full class vtable/registration; actual named source C2 and all base constructors execute with identical final object effects, preserved remaining bytes and callee-saved registers; no fake constructor or field offset rewrite',
                                  'classname': classname, 'method_slots': parent['method_slots'],
                                  'original_vtable': hex(old_vtable), 'native_vtable': hex(new_vtable),
                                  'source_constructor_sha256': hashlib.sha256(source_code).hexdigest(), 'cases': 16})
            except Exception as error:
                refused.append({'classname': classname, 'original': hex(first), 'reason': str(error)})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
                                   'stock_sha256': sha(args.stock_kernel), 'native_sha256': sha(args.native_kernel),
                                   'functions': qualified, 'data': [], 'refused': refused, 'bootable': False,
                                   'replay': {'result': 'PASS_FOR_QUALIFIED_ONLY', 'constructor_input_cases': count,
                                              'PAC': 'host computational abstraction only; real emitted PAC unchanged',
                                              'source_instruction_sites': len(right.executed)}}, indent=2) + '\n')
    print('Complete real constructor/base-chain contracts qualified', len(qualified), 'refused', len(refused), '; host object effects only')


if __name__ == '__main__':
    main()
