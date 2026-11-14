#!/usr/bin/env python3
"""Prove vnode_mount's complete field/PAC contract across compiler forms.

The source uses load-with-writeback instead of load plus address addition.
Both full bodies must retain the same field, DA key, address diversity and
type discriminator. Actual code replays cover null/non-null mount fields,
unaltered objects and callee-saved ABI. Host PAC abstraction is not hardware
authentication or vnode lifetime qualification.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import random
import struct
import sys

OLD = (0xd503245f, 0xf9406c10, 0xb40000f0, 0x91036008, 0xaa0803f1,
       0xf2faddf1, 0xdac11a30, 0xaa1003e0, 0xd65f03c0, 0xd2800000, 0xd65f03c0)
NEW = (0xd503245f, 0xf84d8c10, 0xb4000170, 0xaa0003f1, 0xf2faddf1,
       0xdac11a30, 0xaa1003f1, 0xdac147f1, 0xeb11021f, 0x54000040,
       0xd4388e40, 0xaa1003e0, 0xd65f03c0, 0xd2800000, 0xd65f03c0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'kdk-research', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if args.out.exists() or args.out.resolve() == root or root in args.out.resolve().parents:
        parser.error('Retain prior evidence; choose a new private output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image
    from native18_replay import KernelReplay
    stock_bytes, native_bytes = args.stock_kernel.read_bytes(), args.native_kernel.read_bytes()
    stock_hash, native_hash = (hashlib.sha256(blob).hexdigest() for blob in (stock_bytes, native_bytes))
    cache, native = Image(stock_bytes), Image(native_bytes)
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    def bodies(image):
        ranges = [(row['va'], row['va'] + row['filesize']) for row in image.segments
                  if row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['filesize']]
        starts = sorted(address for address in image.function_starts() if any(first <= address < last for first, last in ranges))
        return {first: image.read(first, last - first) for first, last in zip(starts, starts[1:])
                if last - first <= 64 and any(lo <= first and last <= hi for lo, hi in ranges)}
    old, new = bodies(original), bodies(native)
    expected_old, expected_new = (struct.pack('<' + 'I' * len(words), *words) for words in (OLD, NEW))
    candidates = [address for address, body in old.items() if body == expected_old]
    targets = [address for address, names in native.symbols().items() if '_vnode_mount' in names]
    if len(candidates) != 1 or len(targets) != 1 or new.get(targets[0]) != expected_new:
        raise RuntimeError('Complete vnode_mount field/null/PAC/compiler-check templates differ')
    first, target = candidates[0], targets[0]
    research = json.loads(args.kdk_research.read_text())
    if research.get('reference_role') != 'names-only-research' or research['inputs'][0]['sha256'] != stock_hash:
        raise RuntimeError('KDK corroboration must use this actual stock input')
    leads = [row for row in research['functions'] if row['original'] == hex(first) and '_vnode_mount' in row['names']]
    if len(leads) != 1:
        raise RuntimeError('No unique UUID-matched Apple KDK name corroboration')
    machines = [KernelReplay(original, 64), KernelReplay(native, 64)]
    rng = random.Random(0x22a0d8)
    vectors = []
    for index in range(512):
        obj = 0x50b0000 + 16 * (index % 16)
        mount = 0 if index % 5 == 0 else (0x5000000 + rng.randrange(0x10000) * 16)
        fixture = bytearray(rng.randbytes(0x100))
        struct.pack_into('<Q', fixture, 0xd8, mount)
        saved = {number: rng.getrandbits(64) for number in range(19, 30)}
        for machine, entry in zip(machines, (first, target)):
            machine.uc.mem_write(obj, bytes(fixture))
            result = machine.run(entry, [obj], saved)
            if result != mount or bytes(machine.uc.mem_read(obj, len(fixture))) != fixture or any(machine.uc.reg_read(machine.registers[number]) != value for number, value in saved.items()) or machine.uc.reg_read(machine.reg.UC_ARM64_REG_SP) != machine.stack:
                raise RuntimeError('Actual vnode_mount return/object/callee-saved/SP contract differs')
        vectors.append((obj, mount, hashlib.sha256(fixture).hexdigest()))
    report = {'build': '22G100', 'arch': 'arm64e', 'analysis_only': True, 'bootable': False,
              'observed_at': datetime.now().astimezone().isoformat(), 'device_access': False,
              'stock_sha256': stock_hash, 'native_sha256': native_hash,
              'functions': [{'original': hex(first), 'name': '_vnode_mount', 'final_import_only': True,
                             'evidence': 'Complete stock/source field/null/PAC/compiler-check templates preserve vnode field0xd8, DA key, address-diverse discriminator0xd6ef and read-only mount-pointer result despite source load-writeback;512 actual-code/null-field/alignment/object/ABI cases; UUID-matched Apple KDK corroborates API name; no vnode lifetime or hardware PAC claim',
                             'original_sha256': hashlib.sha256(old[first]).hexdigest(),
                             'native_sha256': hashlib.sha256(new[target]).hexdigest()}], 'data': [],
              'kdk_research_sha256': hashlib.sha256(args.kdk_research.read_bytes()).hexdigest(),
              'replay': {'result': 'PASS', 'cases': len(vectors),
                         'vectors_sha256': hashlib.sha256(json.dumps(vectors).encode()).hexdigest(),
                         'authentication': 'Raw fixture pointers; host PAC abstraction only; all guest authentication/check instructions remain required by full templates',
                         'caller_precondition': 'Valid live vnode pointer; a null mount field is permitted; no object lifetime is created or extended'}}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print('Complete vnode_mount contract:512 actual-code field/null/object/ABI cases PASS; no hardware claim')


if __name__ == '__main__':
    main()
