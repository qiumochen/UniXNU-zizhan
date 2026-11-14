#!/usr/bin/env python3
"""Qualify the actual bounded strnlen used by the OSSymbol NoCopy path.

The stock target is recovered from the actual stock NoCopy call boundary and
the source target is its unique compiled `_strnlen` definition. Complete
vector bodies, mask tables, zero/negative/large bounds, alignment and a real
guard-page fixture are checked with actual image instructions. This produces
one final import contract only; it does not qualify the symbol pool lookup or
the NoCopy factory itself.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import random
import struct
import sys

OLD_PREFIX = (0xd503245f, 0xea01003f, 0x54000544, 0x54000380,
              0xd503237f, 0xa9bf7bfd, 0x910003fd, 0x927cec02,
              0x3dc00040, 0x10000003, 0x3cc10462, 0x92400c04,
              0xcb040063, 0x3dc00061, 0x4ea11c00, 0x8b040021)
NEW_PREFIX = (0xea01003f, 0x540004e4, 0x54000320, 0xd503237f,
              0xa9bf7bfd, 0x910003fd, 0x927cec02, 0x3dc00040,
              0x10000003, 0x3cc10462, 0x92400c04, 0xcb040063,
              0x3dc00061, 0x4ea11c00, 0x8b040021)
BODY = (0x3cc10c40, 0x6e31a801, 0x1e260023, 0x34000143,
        0xf1004021, 0x54ffff68, 0xcb000040, 0x91004021,
        0x8b010000, 0xa8c17bfd, 0xd65f0fff, 0xd2800000,
        0xd65f03c0, 0x6e211c21, 0x6e213400, 0x4ea21c00,
        0x6e31a801, 0x1e260023, 0xcb000040, 0xeb03003f,
        0x9a833021, 0x8b010000, 0xa8c17bfd, 0xd65f0fff)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def body(image, address):
    end = min(value for value in image.function_starts() if value > address)
    return image.read(address, end - address)


def check_template(image, address, code, prefix):
    from kc_image import scan
    words = struct.unpack('<' + 'I' * (len(code) // 4), code)
    _, _, shape = scan(code, address)
    if len(words) not in (40, 44) or tuple(shape[:len(prefix)]) != prefix:
        return False
    # The only body difference is stock's four NOP alignment words before the
    # vector load. Remove those padding words, then require every actual loop,
    # mask-table and return instruction in order.
    start = len(prefix) + 1 + (3 if len(words) == 44 else 0)
    padding = words[len(prefix) + 1:start]
    expected_padding = (0xd503201f,) * 3 if len(words) == 44 else ()
    if shape[len(prefix)] & 0xfc000000 != 0x14000000:
        return False
    if padding != expected_padding or tuple(shape[start:start + len(BODY)]) != BODY:
        return False
    calls, refs, _ = scan(code, address)
    mask_pc = address + (36 if len(words) == 44 else 32)
    return (all(not link and address <= target < address + len(code) for _, target, link in calls) and
            len(refs) == 1 and refs[0][2] == 'adr' and
            refs[0][0] == mask_pc and image.read(refs[0][1], 32) == bytes(range(16)) + bytes(16))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'native-build', 'factory-pairing', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if args.out.exists() or args.out.resolve() == root or root in args.out.resolve().parents:
        parser.error('Use a new private output path')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    sys.path.insert(0, str(root / 'scripts'))
    from kc_image import Image, scan
    from native18_replay import KernelReplay
    stock_bytes, native_bytes = args.stock_kernel.read_bytes(), args.native_kernel.read_bytes()
    cache, native = Image(stock_bytes), Image(native_bytes)
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    built = json.loads((args.native_build / 'host-core.json').read_text())
    if built.get('returncode') != 0 or not any(Path(row['path']).name == args.native_kernel.name and row['sha256'] == sha(args.native_kernel) for row in built['artifacts']):
        parser.error('Require actual completed source image')
    pairing = json.loads(args.factory_pairing.read_text())
    if pairing.get('qualification') != 'INDEPENDENT_CONTRACTS_ONLY' or Path(pairing['original']).resolve() != args.stock_kernel.resolve() or Path(pairing['native']).resolve() != args.native_kernel.resolve():
        parser.error('Require current independent actual-input pairing')
    names = {}
    for address, values in native.symbols().items():
        for name in values:
            names.setdefault(name, set()).add(address)
    if len(names.get('_strnlen', ())) != 1:
        parser.error('Require one actual source _strnlen')
    native_address = next(iter(names['_strnlen']))
    stock_factory = 0xfffffff008797fe0
    stock_code = body(original, stock_factory)
    calls = []
    from kc_image import scan
    calls, _, _ = scan(stock_code, stock_factory)
    stock_targets = [target for site, target, link in calls
                     if link and site == stock_factory + 0x20]
    if stock_targets != [0xfffffff008106ac0]:
        parser.error('Require the actual NoCopy-to-strnlen call boundary')
    stock_address = stock_targets[0]
    old_code, new_code = body(original, stock_address), body(native, native_address)
    if not check_template(original, stock_address, old_code, OLD_PREFIX) or not check_template(native, native_address, new_code, NEW_PREFIX):
        raise RuntimeError('Complete stock/source strnlen vector body or exact mask table differs')
    left, right = KernelReplay(original), KernelReplay(native)
    base = 0x50f0000
    rng = random.Random(0x22f571)
    vectors = []
    for alignment in range(16):
        for length in (0, 1, 2, 7, 15, 16, 17, 31, 32, 63, 127, 255, 1023):
            text = bytes(rng.randrange(1, 256) for _ in range(length)) + b'\0'
            for bound in (0, 1, length // 2, length, length + 1, length + 17, 0x3fffe, 0xffffffffffffffff):
                address = base + alignment
                for machine, entry in ((left, stock_address), (right, native_address)):
                    machine.uc.mem_write(address, bytes([0xa5]) * 2048)
                    machine.uc.mem_write(address, text)
                    result = machine.run(entry, [address, bound])
                    expected = min(length, bound)
                    if result != expected or bytes(machine.uc.mem_read(address, len(text))) != text:
                        raise RuntimeError('Actual strnlen bounded result/object mismatch')
                vectors.append((alignment, length, bound, min(length, bound)))
    # A string ending before an unmapped page must not read beyond the bound.
    for length in (0, 1, 7, 15, 16, 31, 63, 127):
        address = base + 4096 - length - 1
        text = bytes((index * 13 + 7) & 0x7f or 1 for index in range(length)) + b'\0'
        for machine, entry in ((left, stock_address), (right, native_address)):
            machine.uc.mem_write(base, bytes(0x1000))
            machine.uc.mem_write(address, text)
            machine.uc.mem_unmap(base + 0x1000, 0x1000)
            try:
                result = machine.run(entry, [address, length + 1])
            finally:
                machine.uc.mem_map(base + 0x1000, 0x1000)
            if result != length:
                raise RuntimeError('Actual strnlen guard-page result mismatch')
        vectors.append(('guard', length, length + 1, length))
    report = {'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
              'stock_sha256': sha(args.stock_kernel), 'native_sha256': sha(args.native_kernel),
              'functions': [{'original': hex(stock_address), 'name': '_strnlen', 'final_import_only': True,
                             'evidence': 'Actual NoCopy call target is the unique stock vector strnlen body; complete stock/source mask-table/vector loops and zero/negative/large bound semantics agree across 208 alignment/bound vectors plus 8 guard-page cases; actual image object bytes preserved; does not authorize NoCopy factory, symbol-pool lookup, pointer lifetime or hardware qualification',
                             'original_sha256': hashlib.sha256(old_code).hexdigest(), 'native_sha256': hashlib.sha256(new_code).hexdigest(),
                             'stock_factory_callsite': hex(stock_factory)}], 'data': [], 'bootable': False,
              'replay': {'result': 'PASS', 'cases': len(vectors), 'vectors_sha256': hashlib.sha256(json.dumps(vectors).encode()).hexdigest(),
                         'authentication': 'host PAC abstraction only', 'guard_page_cases': 8,
                         'scope': 'actual bounded string computation; no NoCopy allocation or pool mutation'}}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print('Actual stock/source strnlen contract qualified:', len(vectors), 'cases; NoCopy and symbol-pool contracts remain withheld')


if __name__ == '__main__':
    main()
