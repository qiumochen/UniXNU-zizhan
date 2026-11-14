#!/usr/bin/env python3
"""Review complete scalar byte-comparison loops and vector strlen.

Scalar templates differ only in post-indexed loads versus dead argument
pointer increments before the next iteration. All65,536 first-byte pairs
are executed, followed by bounded long-prefix cases. strlen requires the
complete same instruction body and exact32-byte mask table; unreachable
trailing UDF alignment is independently excluded from branch targets.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import random
import struct
import sys

OLD_COMPARE = (0xd503245f, 0x3940000a, 0x13001d48, 0x39400029, 0x6b09015f, 0x540000c1,
               0x91000421, 0x91000400, 0x35ffff28, 0x52800000, 0xd65f03c0, 0x13001d29, 0x4b090100, 0xd65f03c0)
NEW_COMPARE = (0xd503245f, 0x3840140a, 0x13001d48, 0x38401429, 0x6b09015f, 0x54000081,
               0x35ffff68, 0x52800000, 0xd65f03c0, 0x13001d29, 0x4b090100, 0xd65f03c0)
OLD_MEMORY = (0xd503245f, 0xb4000122, 0x39400008, 0x39400029, 0x6b090108, 0x540000c1,
              0x91000421, 0x91000400, 0xf1000442, 0x54ffff21, 0x52800008, 0xaa0803e0, 0xd65f03c0)
NEW_MEMORY = (0xd503245f, 0xb40000e2, 0x38401408, 0x38401429, 0x6b090108, 0x54000081,
              0xf1000442, 0x54ffff61, 0x52800008, 0xaa0803e0, 0xd65f03c0)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'native-build', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain prior string-loop replay; choose a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan, signed
    from native18_replay import KernelReplay
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    built = json.loads((args.native_build / 'host-core.json').read_text())
    if built.get('returncode') != 0 or not any(Path(row['path']).name == args.native_kernel.name and row['sha256'] == sha(args.native_kernel) for row in built['artifacts']):
        parser.error('Require actual completed native source image')
    names = {}
    for address, rows in native.symbols().items():
        for name in rows:
            names.setdefault(name, set()).add(address)
    def named(name):
        if len(names.get(name, ())) != 1:
            raise RuntimeError('Actual source primitive is not unique: ' + name)
        return next(iter(names[name]))
    def bodies(image):
        starts = sorted(a for a in image.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in image.segments))
        return {a: image.read(a, b - a) for a, b in zip(starts, starts[1:]) if 8 <= b - a <= 1024}
    old, new = bodies(original), bodies(native)
    targets = {}
    for name, old_template, new_template in (('_strcmp', OLD_COMPARE, NEW_COMPARE), ('_memcmp', OLD_MEMORY, NEW_MEMORY)):
        candidates = [a for a, code in old.items() if code == struct.pack('<' + 'I' * len(old_template), *old_template)]
        if len(candidates) != 1 or new.get(named(name)) != struct.pack('<' + 'I' * len(new_template), *new_template):
            raise RuntimeError('Complete scalar loop no longer has the reviewed pointer/load/control contract: ' + name)
        targets[name] = candidates[0]
    def strlen_contract(image, address, code):
        # Strip whole trailing UDF words only; a partial zero byte at the
        # end of a real instruction is never padding.
        while len(code) >= 4 and code[-4:] == bytes(4):
            code = code[:-4]
        if len(code) != 100 or code[-4:] != struct.pack('<I', 0xd65f0fff):
            return None
        calls, refs, shape = scan(code, address)
        if len(refs) != 1 or refs[0][2] != 'adr' or refs[0][0] != address + 20:
            return None
        if image.read(refs[0][1], 32) != bytes(range(16)) + bytes(16):
            return None
        # Full vector loop is compared below; make sure no control edge
        # enters alignment UDF bytes removed from its complete extent.
        for offset in range(0, len(code), 4):
            word, = struct.unpack_from('<I', code, offset)
            bits, shift = (26, 0) if word & 0x7c000000 == 0x14000000 else (19, 5) if word & 0xff000010 == 0x54000000 or word & 0x7e000000 == 0x34000000 else (0, 0)
            if bits:
                destination = offset + signed((word >> shift) & ((1 << bits) - 1), bits) * 4
                if not 0 <= destination < len(code):
                    return None
        return tuple(shape), code, refs[0][1]
    source_strlen = strlen_contract(native, named('_strlen'), new[named('_strlen')])
    if not source_strlen:
        raise RuntimeError('Actual source vector strlen/mask/return contract differs')
    strlen_candidates = []
    for address, code in old.items():
        result = strlen_contract(original, address, code)
        if result and result[0] == source_strlen[0]:
            strlen_candidates.append((address, result))
    if len(strlen_candidates) != 1:
        raise RuntimeError('Actual complete vector strlen identity is ambiguous')
    targets['_strlen'] = strlen_candidates[0][0]
    left, right = KernelReplay(original), KernelReplay(native)
    a, b = 0x50a0000, 0x50a4000
    cases = {'_strcmp': 0, '_memcmp': 0, '_strlen': 0}
    def compare(name, first, second, count=None):
        expected = 0
        if name == '_strcmp':
            for x, y in zip(first, second):
                if x != y:
                    # Match the actual kernel's signed-char strcmp source
                    # loop. This is not a claim about userspace libc.
                    expected = (x if x < 128 else x - 256) - (y if y < 128 else y - 256)
                    break
                if x == 0:
                    break
        else:
            for x, y in zip(first[:count], second[:count]):
                if x != y:
                    expected = x - y
                    break
        arguments = [a, b] + ([] if count is None else [count])
        for machine, entry in ((left, targets[name]), (right, named(name))):
            machine.uc.mem_write(a, first)
            machine.uc.mem_write(b, second)
            result = machine.run(entry, arguments) & 0xffffffff
            if result != expected & 0xffffffff:
                raise RuntimeError('Actual scalar compare disagrees with complete-loop byte model: ' + repr((name, first.hex(), second.hex(), count, result, expected)))
            if bytes(machine.uc.mem_read(a, len(first))) != first or bytes(machine.uc.mem_read(b, len(second))) != second:
                raise RuntimeError('Actual scalar comparison mutated its input')
        cases[name] += 1
    for x in range(256):
        for y in range(256):
            first, second = bytes((x, 0)), bytes((y, 0))
            compare('_strcmp', first, second)
            compare('_memcmp', first, second, 1)
    rng = random.Random(0x22b17e)
    for _ in range(256):
        prefix = bytes(rng.randrange(1, 256) for _ in range(rng.randrange(128)))
        first = prefix + bytes((rng.randrange(256), 0))
        second = prefix + bytes((rng.randrange(256), 0))
        compare('_strcmp', first, second)
        compare('_memcmp', first, second, rng.randrange(len(prefix) + 2))
    for offset in range(16):
        for length in (0, 1, 2, 7, 15, 16, 17, 31, 32, 63, 64, 127, 255, 511):
            text = bytes(rng.randrange(1, 256) for _ in range(length)) + b'\0'
            for machine, entry in ((left, targets['_strlen']), (right, named('_strlen'))):
                machine.uc.mem_write(a, bytes([0xa5]) * 1024)
                machine.uc.mem_write(a + offset, text)
                if machine.run(entry, [a + offset]) != length:
                    raise RuntimeError('Actual vector strlen disagrees on length/alignment')
            cases['_strlen'] += 1
    for length in (0, 1, 7, 15, 16, 31, 255, 511):
        address = a + 4095 - length
        text = bytes(rng.randrange(1, 256) for _ in range(length)) + b'\0'
        for machine, entry in ((left, targets['_strlen']), (right, named('_strlen'))):
            machine.uc.mem_write(a, bytes([0xa5]) * 4096)
            machine.uc.mem_write(address, text)
            machine.uc.mem_unmap(a + 4096, 4096)
            try:
                if machine.run(entry, [address]) != length:
                    raise RuntimeError('Actual vector strlen guard-page length differs')
            finally:
                machine.uc.mem_map(a + 4096, 4096)
        cases['_strlen'] += 1
    rows = [{'original': hex(address), 'name': name, 'final_import_only': True,
             'evidence': 'Complete concrete scalar byte-loop/control/return templates or identical complete vector strlen plus exact32-byte mask table; pointer post-indexing changes only dead caller-clobbered argument state; exhaustive65,536 byte pairs and bounded long-prefix/alignment cases execute actual images against independent model',
             'original_sha256': hashlib.sha256(old[address]).hexdigest(), 'native_sha256': hashlib.sha256(new[named(name)]).hexdigest(),
             'cases': cases[name]} for name, address in targets.items()]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
                                   'stock_sha256': sha(args.stock_kernel), 'native_sha256': sha(args.native_kernel),
                                   'functions': rows, 'data': [], 'bootable': False,
                                   'replay': {'result': 'PASS', 'cases': cases, 'authentication': 'host computational abstraction only',
                                              'strcmp_semantics': 'actual kernel signed-char loop, not userspace libc guarantee'}}, indent=2) + '\n')
    print('Actual complete comparison/strlen contracts qualified:', cases, flush=True)


if __name__ == '__main__':
    main()
