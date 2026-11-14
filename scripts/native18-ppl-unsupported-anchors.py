#!/usr/bin/env python3
"""Review named unsupported PPL KPIs and their actual false-gated consumers.

Uses only the real non-PPL XNU implementations. Stock panics and source
NULL/no-op differ: require the complete actual retained corecrypto allocation
and free call paths to select their ordinary IOMallocData/IOFreeData branches
under an independently verified constant-false query. No PPL support claimed.
"""
import argparse
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
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'pairing', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain prior review; choose a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan, signed
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    pairing = json.loads(args.pairing.read_text())
    if pairing.get('build') != '22G100' or Path(pairing['original']).resolve() != args.stock_kernel.resolve() or Path(pairing['native']).resolve() != args.native_kernel.resolve():
        parser.error('Require fresh actual-input pairing')
    names = {}
    for address, rows in native.symbols().items():
        for name in rows:
            names.setdefault(name, set()).add(address)
    def named(name):
        if len(names.get(name, ())) != 1:
            raise RuntimeError('Native KPI definition not unique: ' + name)
        return next(iter(names[name]))
    def body(image, address):
        starts = sorted(a for a in image.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in image.segments))
        index = starts.index(address)
        return image.read(address, starts[index + 1] - address)
    def words(code):
        return struct.unpack('<' + 'I' * (len(code) // 4), code)
    expected_native = {'_pmap_in_ppl': (0xd503245f, 0x52800000, 0xd65f03c0),
                       '_pmap_claim_reserved_ppl_page': (0xd503245f, 0xd2800000, 0xd65f03c0),
                       '_pmap_free_reserved_ppl_page': (0xd503245f, 0xd65f03c0)}
    for name, expected in expected_native.items():
        if words(body(native, named(name))) != expected:
            raise RuntimeError('Review changed actual non-PPL KPI implementation: ' + name)
    functions = {int(key, 16): value for key, value in pairing['functions'].items()}
    candidates = {}
    starts = sorted(address for address in original.function_starts()
                    if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= address < row['va'] + row['filesize'] for row in original.segments))
    for address, end in zip(starts, starts[1:]):
        if end - address != 56:
            continue
        # Stock unsupported PANIC wrappers are complete 56-byte functions.
        code = original.read(address, 56)
        calls, refs, shape = scan(code, address)
        literals = {original.cstring(value) for _, value, _ in refs}
        for name in ('_pmap_claim_reserved_ppl_page', '_pmap_free_reserved_ppl_page'):
            if {name[1:], 'pmap.c', '%s: function not supported in this environment @%s:%d'} <= literals:
                normalized = list(shape)
                normalized[4] &= 0xffe0001f  # the source line number only
                expected = (0xd503237f, 0xd100c3ff, 0xa9027bfd, 0x910083fd, 0x52800008,
                            0x90000009, 0x91000129, 0x9000000a, 0x9100014a, 0xa900a3e9,
                            0xf90003ea, 0x90000000, 0x91000000, 0x94000000)
                if tuple(normalized) != expected or len(refs) != 3 or len(calls) != 1 or calls[0][0] != address + 52 or not calls[0][2] or functions.get(calls[0][1], {}).get('name') != '_panic':
                    raise RuntimeError('Stock unsupported wrapper is not a complete fatal-panic body')
                if name in candidates:
                    raise RuntimeError('Stock unsupported KPI identity is ambiguous')
                candidates[name] = address
    if len(candidates) != 2:
        raise RuntimeError('Both explicitly named actual stock unsupported KPIs are required')
    crypto = Image(cache.data, cache.entries['com.apple.kec.corecrypto'][1])
    references = []
    for owner, (_, offset) in cache.entries.items():
        if owner == 'com.apple.kernel':
            continue
        image = Image(cache.data, offset)
        for segment in image.segments:
            if segment['name'] != '__TEXT_EXEC' or not segment['filesize']:
                continue
            calls, _, _ = scan(image.read(segment['va'], segment['filesize']), segment['va'])
            for site, target, link in calls:
                if target in candidates.values():
                    references.append((owner, site, target, link))
    if len(references) != 2 or any(owner != 'com.apple.kec.corecrypto' for owner, *_ in references):
        raise RuntimeError('Unexpected live branch consumer of unsupported PPL KPI')
    caller_evidence = []
    for owner, site, target, link in references:
        starts = sorted(crypto.function_starts())
        first = max(a for a in starts if a <= site)
        code = body(crypto, first)
        calls, _, _ = scan(code, first)
        calls = [(pc, callee, link) for pc, callee, link in calls if not first <= callee < first + len(code)]
        # Both real wrappers call panic_active first, then a false query.
        # Under false they branch to the ordinary allocator/free block.
        if len(calls) < 3 or functions.get(calls[0][1], {}).get('name') != '_panic_active':
            raise RuntimeError('Review changed corecrypto panic/allocation path')
        query_site, query, query_link = calls[1]
        if not query_link or words(body(original, query)) != expected_native['_pmap_in_ppl']:
            raise RuntimeError('Actual stock PPL consumer query is not constant false')
        paired = functions.get(query)
        if not paired or words(body(native, int(paired['native'], 16))) != expected_native['_pmap_in_ppl']:
            raise RuntimeError('Mapped actual query no longer returns false')
        branch, = struct.unpack('<I', crypto.read(query_site + 4, 4))
        if branch & 0xff00001f != 0x34000000:
            raise RuntimeError('PPL query must directly gate CBZ w0 fallback')
        fallback = query_site + 4 + signed((branch >> 5) & 0x7ffff, 19) * 4
        expected = '_IOMallocData' if link else '_IOFreeData'
        fallback_calls = [(pc, functions.get(callee, {}).get('name')) for pc, callee, _ in calls if pc >= fallback]
        if not any(name == expected and fallback <= pc <= fallback + 32 for pc, name in fallback_calls) or not query_site < site < fallback:
            raise RuntimeError('False path does not bypass PPL for ordinary data allocation/free')
        if link:
            # Actual allocation callback checks NULL before bzero/dereference.
            after = crypto.read(site + 4, 20)
            if struct.pack('<I', 0xb4000100) not in after:
                raise RuntimeError('Actual PPL allocation callback no longer preserves NULL refusal')
        caller_evidence.append({'owner': owner, 'function': hex(first), 'sha256': hashlib.sha256(code).hexdigest(),
                                'query': hex(query), 'query_result': False, 'fallback': expected,
                                'ppl_edge_unreachable_under_actual_query': True})
    rows = []
    for name, address in candidates.items():
        auth = {pointer[1:] for pointer in cache.fixups().values() if pointer[0] == address}
        if auth != {(1, 0, 0, 1)}:
            raise RuntimeError('Review changed unsupported KPI pointer transport')
        rows.append({'original': hex(address), 'name': name, 'final_import_only': True,
                     'evidence': 'Stock complete fatal-panic body explicitly names this KPI/file; real source non-PPL NULL/no-op implementation verified; both actual retained corecrypto consumers select ordinary allocation/free under verified false query, allocation preserves NULL refusal; no PPL backend or success is provided',
                     'platform_difference': 'stock unsupported panic versus real source unsupported NULL/no-op; guarded paths proven unreachable under current source/stock query',
                     'native_sha256': hashlib.sha256(body(native, named(name))).hexdigest()})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'build': '22G100', 'arch': 'arm64e', 'stock_sha256': sha(args.stock_kernel),
                                   'native_sha256': sha(args.native_kernel), 'observed_at': datetime.now().astimezone().isoformat(),
                                   'functions': rows, 'data': [], 'consumers': caller_evidence,
                                   'ppl_supported': False, 'bootable': False}, indent=2) + '\n')
    print('Named unsupported PPL imports reviewed with real false-gated corecrypto fallbacks; PPL remains unsupported')


if __name__ == '__main__':
    main()
