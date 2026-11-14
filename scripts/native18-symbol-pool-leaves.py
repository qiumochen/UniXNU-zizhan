#!/usr/bin/env python3
"""Qualify OSSymbolPool hash/equality leaves from the actual images.

The source hash and object-equality helpers are executed with their real
instructions against bounded fixtures and an independent Jenkins/byte model.
The actual source memcmp target is allowed only when it is an actual qualified
function. `try_get` is inspected and named, but its retain/SMR lifetime is not
turned into a host success result. No NoCopy/global initializer or device
capability is emitted by this tool.
"""
# Historical proof was withdrawn: preserve replay research, never authorize imports.
# See docs/progress/2026-11-14-native-symbol-pool-correction.md.
import argparse
from datetime import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import struct
import sys

STOCK = {'hash': 0xfffffff008796f34, 'equ': 0xfffffff008796ec8, 'try_get': 0xfffffff008796e7c}
KDK_NAMES = {
    'hash': '__ZN12OSSymbolPool17OSSymbol_obj_hashEPK10smrq_slinkj',
    'equ': '__ZN12OSSymbolPool16OSSymbol_obj_equEPK10smrq_slink10smrh_key_t',
    'try_get': '__ZN12OSSymbolPool20OSSymbol_obj_try_getEPv',
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def body(image, address):
    end = min(value for value in image.function_starts() if value > address)
    return image.read(address, end - address)


def jenkins(data, seed):
    value = seed & 0xffffffff
    for byte in data:
        value = (value + byte) & 0xffffffff
        value = (value + ((value << 10) & 0xffffffff)) & 0xffffffff
        value ^= value >> 6
    value = (value + ((value << 3) & 0xffffffff)) & 0xffffffff
    value ^= value >> 11
    return (value + ((value << 15) & 0xffffffff)) & 0xffffffff


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'native-build',
                 'qualified-pairing', 'kdk-research', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if args.out.exists() or args.out.resolve() == root or root in args.out.resolve().parents:
        parser.error('Use a new private output path')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    sys.path.insert(0, str(root / 'scripts'))
    from kc_image import Image, scan
    from native18_replay import KernelReplay
    stock_blob, native_blob = args.stock_kernel.read_bytes(), args.native_kernel.read_bytes()
    cache, native = Image(stock_blob), Image(native_blob)
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    built = json.loads((args.native_build / 'host-core.json').read_text())
    if built.get('returncode') != 0 or not any(Path(row['path']).name == args.native_kernel.name and row['sha256'] == sha(args.native_kernel) for row in built['artifacts']):
        parser.error('Require actual completed native image')
    pairing = json.loads(args.qualified_pairing.read_text())
    if pairing.get('qualification') != 'INDEPENDENT_CONTRACTS_ONLY' or Path(pairing['original']).resolve() != args.stock_kernel.resolve() or Path(pairing['native']).resolve() != args.native_kernel.resolve():
        parser.error('Require current independent actual-input pairing')
    research = json.loads(args.kdk_research.read_text())
    if research.get('reference_role') != 'names-only-research' or research['inputs'][0]['sha256'] != sha(args.stock_kernel):
        parser.error('Require actual-stock same-XNU KDK research')
    names = {}
    for address, values in native.symbols().items():
        for name in values:
            names.setdefault(name, set()).add(address)
    for key, name in KDK_NAMES.items():
        rows = [row for row in research['functions'] if name in row['names'] and row['original'] == hex(STOCK[key])]
        if len(rows) != 1:
            parser.error('KDK research does not uniquely corroborate ' + name)
    native_addresses = {}
    for key, name in KDK_NAMES.items():
        if len(names.get(name, ())) != 1:
            parser.error('Native source symbol is not unique: ' + name)
        native_addresses[key] = next(iter(names[name]))
    # Actual source equality calls its actual memcmp symbol. It must already
    # be present in the independent map, otherwise this leaf remains review.
    memcmp = [int(key, 16) for key, row in pairing['functions'].items() if row.get('name') == '_memcmp']
    if len(memcmp) != 1:
        parser.error('Require independently qualified concrete _memcmp for object equality')
    left, right = KernelReplay(original), KernelReplay(native)
    rng = random.Random(0x22f001)
    vectors = []
    for index in range(512):
        length = rng.randrange(0, 96)
        seed = rng.getrandbits(32)
        text = bytes(rng.randrange(256) for _ in range(length))
        object_address = 0x50b4000
        string_address = 0x50c0000
        # OSSymbol's link is eight bytes into the object; the length field is
        # the packed field immediately before the authenticated string slot.
        for machine, image, hash_entry, equ_entry in ((left, original, STOCK['hash'], STOCK['equ']),
                                                       (right, native, native_addresses['hash'], native_addresses['equ'])):
            machine.uc.mem_write(string_address, text)
            machine.uc.mem_write(object_address, bytes(0x100))
            machine.uc.mem_write(object_address + 0xc, struct.pack('<I', (length + 1) << 14))
            machine.uc.mem_write(object_address + 0x10, struct.pack('<Q', string_address))
            link = object_address + 0x18
            hash_result = machine.run(hash_entry, [link, seed])
            expected_hash = jenkins(text, seed)
            if hash_result != expected_hash:
                raise RuntimeError('Actual OSSymbol_obj_hash differs from independent Jenkins model')
            key_address = 0x50d0000
            machine.uc.mem_write(key_address, text)
            equ_result = machine.run(equ_entry, [link, key_address, length])
            if equ_result != 1:
                raise RuntimeError('Actual OSSymbol_obj_equ equal-key result differs')
            if length:
                other = bytearray(text)
                other[index % length] ^= 1
                machine.uc.mem_write(key_address, bytes(other))
                if machine.run(equ_entry, [link, key_address, length]) != 0:
                    raise RuntimeError('Actual OSSymbol_obj_equ mismatch-key result differs')
        vectors.append((length, seed, hashlib.sha256(text).hexdigest()))
    # try_get remains a structural review only. It may return permanent true
    # or call taggedTryRetain; no host fixture is allowed to fake its lifetime.
    try_code = body(native, native_addresses['try_get'])
    try_calls, try_refs, _ = scan(try_code, native_addresses['try_get'])
    if len(try_code) != 28 or len(try_calls) != 1 or try_refs:
        raise RuntimeError('Actual OSSymbol_obj_try_get shape/dependency changed')
    report = {'review_only': True, 'binding_authorized': False,
              'qualification': 'REJECTED_HISTORICAL_PROOF', 'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
              'stock_sha256': sha(args.stock_kernel), 'native_sha256': sha(args.native_kernel),
              'functions': [
                  {'original': hex(STOCK['hash']), 'name': KDK_NAMES['hash'], 'final_import_only': True,
                   'evidence': 'Actual stock/source OSSymbol_obj_hash complete bodies read the authenticated string field and packed length, apply the actual Jenkins byte loop with supplied seed, and return the independent model result across512 actual-code fixtures; KDK name corroboration only',
                   'native': hex(native_addresses['hash'])},
                  {'original': hex(STOCK['equ']), 'name': KDK_NAMES['equ'], 'final_import_only': True,
                   'evidence': 'Actual stock/source OSSymbol_obj_equ complete bodies check packed length and invoke the actual qualified memcmp path;512 equal/mismatch byte fixtures agree; no SMR traversal or object lifetime is faked',
                   'native': hex(native_addresses['equ'])},
                  {'original': hex(STOCK['try_get']), 'name': KDK_NAMES['try_get'], 'review_only': True,
                   'evidence': 'Actual source try_get has the permanent-flag fast path and one real taggedTryRetain call; structural review only because host replay cannot replace retain/SMR lifetime',
                   'native': hex(native_addresses['try_get'])}],
              'data': [], 'bootable': False, 'device_access': False,
              'replay': {'result': 'PASS', 'hash_equ_cases': len(vectors), 'vectors_sha256': hashlib.sha256(json.dumps(vectors).encode()).hexdigest(),
                         'try_get': 'REVIEW_ONLY_UNQUALIFIED', 'pac': 'host computational abstraction only'},
              'dependencies': {'memcmp_stock': hex(memcmp[0]), 'try_get_external_calls': [hex(target) for _, target, _ in try_calls],
                               'smr_allocator_lifetime_authorized': False}}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print('Actual OSSymbol hash/equality leaves:512 cases PASS; try_get structural review only')


if __name__ == '__main__':
    main()
