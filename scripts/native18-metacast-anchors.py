#!/usr/bin/env python3
"""Review complete safeMetaCast loop with actual typed getter implementations.

Requires both complete instruction templates and an independently qualified
OSObject metaclass/getMetaClass PAC slot. Host fixtures supply only object
and acyclic superclass metadata; virtual calls execute actual old/source
getter code. No fake method returns, runtime object or hardware is published.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import random
import struct
import sys

OLD = (0xd503245f, 0xb4000320, 0xd503237f, 0xa9be4ff4, 0xa9017bfd, 0x910043fd,
       0xaa0103f3, 0xf9400010, 0xaa0003f1, 0xf2f9b431, 0xdac11a30, 0xf8438e08,
       0xaa1003e9, 0xaa0003f4, 0xaa0903f1, 0xf2ffff91, 0xd73f0911, 0xeb13001f,
       0x54000080, 0xf9400800, 0xb5ffffa0, 0x14000002, 0xaa1403e0, 0xa9417bfd,
       0xa8c24ff4, 0xd50323ff, 0xd65f03c0)
NEW = (0xd503237f, 0xa9be4ff4, 0xa9017bfd, 0x910043fd, 0xaa0003f3, 0xb40001e0,
       0xaa0103f4, 0xf9400270, 0xaa1303f1, 0xf2f9b431, 0xdac11a30, 0xf8438e08,
       0xaa1303e0, 0xf2ffff90, 0xd73f0910, 0xeb00029f, 0x54000080, 0xf9400800,
       0xb5ffffa0, 0xd2800013, 0xaa1303e0, 0xa9417bfd, 0xa8c24ff4, 0xd65f0fff)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'class-abi', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain old metacast replay; choose a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    from native18_replay import KernelReplay
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    receipt = json.loads((args.class_abi / 'receipt.json').read_text())
    if (receipt['stock_sha256'], receipt['native_sha256']) != (sha(args.stock_kernel), sha(args.native_kernel)):
        parser.error('Actual kernel/class evidence hashes required')
    names = {}
    for address, rows in native.symbols().items():
        for name in rows:
            names.setdefault(name, set()).add(address)
    def named(name):
        if len(names.get(name, ())) != 1:
            raise RuntimeError('Actual source type/method must be unique: ' + name)
        return next(iter(names[name]))
    def bodies(image):
        starts = sorted(a for a in image.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in image.segments))
        return {a: image.read(a, b - a) for a, b in zip(starts, starts[1:]) if b - a <= 512}
    old, new = bodies(original), bodies(native)
    candidates = [address for address, code in old.items() if len(code) == len(OLD) * 4 and tuple(scan(code, address)[2]) == OLD]
    method = '__ZN15OSMetaClassBase12safeMetaCastEPKS_PK11OSMetaClass'
    target = named(method)
    if len(candidates) != 1 or target not in new or tuple(scan(new[target], target)[2]) != NEW:
        raise RuntimeError('Complete actual safeMetaCast typed-vcall/null/superclass loop differs')
    start = candidates[0]
    classes = []
    for row in receipt['results']:
        if not row.get('qualified'):
            continue
        parent = json.loads((args.class_abi / row['path']).read_text())
        classname = parent['classname']
        symbol = '__ZN' + str(len(classname)) + classname + '10gMetaClassE'
        if symbol in names and len(names[symbol]) == 1:
            classes.append((classname, int(parent['metaclass'], 16), named(symbol), parent))
    base = next(row for row in classes if row[0] == 'OSObject')
    getter = next(row for row in base[3]['methods'] if row['slot'] == 7)
    if getter['diversity'] != 0xfffc or getter['key'] != 0 or getter['address_diversity'] != 1:
        raise RuntimeError('Actual getMetaClass typed virtual slot differs')
    old_getter, new_getter = int(getter['address'], 16), named(getter['name'])
    for image, address, meta in ((original, old_getter, base[1]), (native, new_getter, base[2])):
        code = (old if image is original else new).get(address)
        if not code or len(code) != 16:
            raise RuntimeError('Actual getter must remain a complete constant-metaclass accessor')
        calls, refs, shape = scan(code, address)
        if calls or len(refs) != 1 or refs[0][1:] != (meta, 'add') or tuple(shape) != (0xd503245f, 0x90000000, 0x91000000, 0xd65f03c0):
            raise RuntimeError('Actual getter/metaclass identity disagrees with complete class evidence')
    left, right = KernelReplay(original), KernelReplay(native)
    rng = random.Random(0x22ca57)
    vectors = []
    others = [row for row in classes if row[0] != 'OSObject']
    for index in range(512):
        chain = [base] + rng.sample(others, rng.randrange(8))
        requested = rng.choice(chain + [None, rng.choice(others)])
        null_object = index % 17 == 0
        expected = 0 if null_object or requested not in chain else 0x50b0000
        saved = {number: rng.getrandbits(64) for number in range(19, 30)}
        for machine, entry, callback, meta_index in ((left, start, old_getter, 1), (right, target, new_getter, 2)):
            # Actual getters return their real per-image metadata address.
            # Fixture superclass links are data only, not replacement code.
            for position, row in enumerate(chain):
                following = chain[position + 1][meta_index] if position + 1 < len(chain) else 0
                machine.uc.mem_write(row[meta_index] + 0x10, struct.pack('<Q', following))
            table, obj = 0x50c0000, 0x50b0000
            machine.uc.mem_write(table, bytes(128))
            machine.uc.mem_write(table + 0x38, struct.pack('<Q', callback))
            machine.uc.mem_write(obj, struct.pack('<Q', table) + bytes([0x5a]) * 120)
            before = bytes(machine.uc.mem_read(obj, 128))
            requested_address = requested[meta_index] if requested else 0
            result = machine.run(entry, [0 if null_object else obj, requested_address], saved)
            if result != expected or bytes(machine.uc.mem_read(obj, 128)) != before or any(machine.uc.reg_read(machine.registers[number]) != value for number, value in saved.items()):
                raise RuntimeError('Actual safeMetaCast object/target/superclass/ABI result disagrees')
        vectors.append({'chain': [row[0] for row in chain], 'requested': requested[0] if requested else None,
                        'null_object': null_object, 'matched': bool(expected)})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
                                   'stock_sha256': sha(args.stock_kernel), 'native_sha256': sha(args.native_kernel),
                                   'functions': [{'original': hex(start), 'name': method, 'final_import_only': True,
                                                  'evidence': 'Complete stock/source safeMetaCast typed-getter/null/superclass templates; independently qualified actual OSObject getMetaClass slot and getter execute in both images;512 acyclic superclass/target/null data fixtures agree with direct hierarchy model, object bytes and callee-saved registers preserved; no fake method code or field offset relaxation',
                                                  'original_sha256': hashlib.sha256(old[start]).hexdigest(), 'native_sha256': hashlib.sha256(new[target]).hexdigest()}],
                                   'data': [], 'bootable': False,
                                   'replay': {'result': 'PASS', 'cases': len(vectors), 'vectors_sha256': hashlib.sha256(json.dumps(vectors).encode()).hexdigest(),
                                              'actual_getter_virtual_calls': len(left.transfers) + len(right.transfers),
                                              'authentication': 'host computational abstraction only; actual guest PAC unchanged',
                                              'metadata': 'host-only acyclic links written into actual metadata locations; no guest object publication'}}, indent=2) + '\n')
    print('Actual complete safeMetaCast/getter contract qualified;512 hierarchy/null/ABI cases; no guest objects published')


if __name__ == '__main__':
    main()
