#!/usr/bin/env python3
"""Identify actual global slots initialized from the same literal/factory.

Only already reviewed paired initializer functions qualify. Require literal
x0 immediately before the same independently paired factory call, then a
bounded straight-line return-x0 store with no x0 clobber. Both literal/call
and store must be unique within the paired initializer. No neighboring BSS
offset, pointer-table order or fabricated initialized value is inferred.
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
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'pairing', 'imports', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--class-abi', type=Path,
                        help='Actual complete OSSymbol class evidence for independent CStringNoCopy factory review')
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain prior evidence; choose a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    pairing = json.loads(args.pairing.read_text())
    if pairing.get('qualification') != 'INDEPENDENT_CONTRACTS_ONLY':
        parser.error('Symbol factory/global contracts require an independent qualified map; research callee names are insufficient')
    if pairing.get('build') != '22G100' or Path(pairing['original']).resolve() != args.stock_kernel.resolve() or Path(pairing['native']).resolve() != args.native_kernel.resolve():
        parser.error('Require actual-input fresh pairing')
    imports = json.loads(args.imports.read_text())
    missing = {int(key, 16) for rows in imports['missing'].values() for key in rows}
    symbols = native.symbols()
    names = defaultdict(set)
    for address, rows in symbols.items():
        for name in rows:
            names[name].add(address)
    pairs = {int(address, 16): int(row['native'], 16) for address, row in pairing['functions'].items()
             if row.get('how') in ('reviewed', 'vtable', 'qualified-direct-contract')}
    # These real immutable-name factories consume one CString and return the
    # canonical interned symbol/registry plane; their identity must already
    # be present in the reviewed function map.
    factories = {'__ZN8OSSymbol17withCStringNoCopyEPKc', '__ZN15IORegistryEntry9makePlaneEPKc'}
    native_factories = {next(iter(names[name])): name for name in factories if len(names.get(name, ())) == 1}
    old_factories = {address: native_factories[target] for address, target in pairs.items() if target in native_factories}
    def bodies(image):
        starts = sorted(a for a in image.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in image.segments))
        return {a: (b, image.read(a, b - a)) for a, b in zip(starts, starts[1:]) if b - a <= 65536}
    old, new = bodies(original), bodies(native)
    factory_evidence = []
    if args.class_abi:
        receipt = json.loads((args.class_abi / 'receipt.json').read_text())
        parent = json.loads((args.class_abi / 'OSSymbol.json').read_text())
        if (receipt['stock_sha256'], receipt['native_sha256']) != (sha(args.stock_kernel), sha(args.native_kernel)) or not parent.get('abi_compatible'):
            parser.error('Require complete actual-input OSSymbol PAC/class evidence')
        old_meta = int(parent['metaclass'], 16)
        new_meta = next(iter(names['__ZN8OSSymbol10gMetaClassE']))
        # Distinguish NoCopy from the copying factory: same bounded length
        # lookup prefix is insufficient. Also require actual OSSymbol meta
        # allocation followed immediately by unconditional NoCopy/length
        # fields and the authenticated original-string pointer store.
        prefix = (0xaa0003f3, 0x321f43e1, 0x94000000, 0x529fffa8, 0x72a00068,
                  0xeb08001f, 0x54000069, 0xd2800000, 0x14000000, 0xaa0003f4,
                  0x90000000, 0x91000000, 0xaa1303e1, 0xaa1403e2, 0x94000000)
        pointer = (0xb9000c08, 0x91004008, 0xb40000d3, 0xaa0803f0, 0xf2e3e1d0,
                   0xaa1303f1, 0xdac10a11, 0x14000002, 0xd2800011, 0xf9000111)
        flags = ((0x52800028, 0x2a163908), (0x52880028, 0x0b143908))
        source_factory = next(iter(names['__ZN8OSSymbol17withCStringNoCopyEPKc']))
        def factory_contract(image, address, item, meta, original_side):
            end, code = item
            starts = [offset for offset in range(0, min(36, len(code) - 60), 4)
                      if scan(code[offset:offset + 60], address + offset)[2] == list(prefix)]
            if len(starts) != 1:
                return None
            offset = starts[0]
            if struct.unpack_from('<I', code, offset + 32)[0] != 0x14000008:
                return None
            calls, refs, _ = scan(code[offset:offset + 60], address + offset)
            if len(calls) != 3 or not calls[0][2] or calls[1][2] or not calls[2][2] or len(refs) != 1:
                return None
            # Existing source/stock callee pairing is checked explicitly;
            # it is corroboration of this argument/length contract, not a
            # graph-derived declaration of the factory itself.
            for index, expected in ((0, '_strnlen'), (2, '__ZNK12OSSymbolPool10findSymbolE10smrh_key_t')):
                target = calls[index][1]
                if original_side:
                    row = pairing['functions'].get(hex(target))
                    if not row or row.get('name') != expected or int(row['native'], 16) not in names.get(expected, ()):
                        return None
                elif target not in names.get(expected, ()):
                    return None
            target_words = flags[0 if original_side else 1] + pointer
            binary = struct.pack('<' + 'I' * len(target_words), *target_words)
            locations = [index for index in range(4, len(code) - len(binary) + 1, 4)
                         if code[index:index + len(binary)] == binary]
            if len(locations) != 1:
                return None
            fields = locations[0]
            before, = struct.unpack_from('<I', code, fields - 4)
            if before & 0xffc003ff != 0xf90003e0:
                return None
            _, all_refs, _ = scan(code, address)
            meta_refs = [site for site, value, kind in all_refs if value == meta and kind == 'add' and address < site < address + fields]
            if len(meta_refs) != 1 or not 16 <= address + fields - meta_refs[0] <= 64:
                return None
            allocation = image.read(meta_refs[0], address + fields - meta_refs[0])
            allocation_words = struct.unpack('<' + 'I' * (len(allocation) // 4), allocation)
            if not any(word in (0xdac11a30,) for word in allocation_words) or not any(word & 0xffe0fc00 == 0xd7200800 for word in allocation_words):
                return None
            # The stock compiler may place the x22=len+1 preparation before
            # the common lookup prefix; the exact NoCopy fields above are the
            # authoritative distinction from the copying factory.
            return {'bounded_length_and_pool_prefix_sha256': hashlib.sha256(code[offset:offset + 60]).hexdigest(),
                    'nocopy_flags_and_authenticated_string_store_sha256': hashlib.sha256(binary).hexdigest(),
                    'symbol_metaclass': hex(meta), 'pool': hex(refs[0][1]),
                    'length_bound': 0x3fffe, 'string_pointer_diversity': 0x1f0e}
        actual_source = factory_contract(native, source_factory, new[source_factory], new_meta, False)
        if not actual_source:
            raise RuntimeError('Actual source NoCopy factory contract changed')
        found = [(address, value) for address, item in old.items()
                 if 128 <= len(item[1]) <= 1024 and (value := factory_contract(original, address, item, old_meta, True))]
        if len(found) != 1:
            raise RuntimeError('Actual stock NoCopy factory identity is ambiguous/refused')
        address, contract = found[0]
        current = pairing['functions'].get(hex(address))
        if current and int(current['native'], 16) != source_factory:
            raise RuntimeError('Concrete NoCopy factory contradicts current pairing')
        old_factories[address] = '__ZN8OSSymbol17withCStringNoCopyEPKc'
        factory_evidence.append({'original': hex(address), 'name': '__ZN8OSSymbol17withCStringNoCopyEPKc',
                                 'evidence': 'Bounded CString length/pool lookup prefix plus actual qualified OSSymbol metaclass allocation, unconditional NoCopy/length fields and authenticated original-string store; copying factory not accepted by shared prefix alone',
                                 'original_contract': contract, 'native_contract': actual_source})
    def stores(image, address, item, known_factories):
        end, code = item
        calls, refs, _ = scan(code, address)
        results = defaultdict(list)
        by_site = {site: (value, kind) for site, value, kind in refs}
        for site, callee, link in calls:
            if not link or callee not in known_factories:
                continue
            before = by_site.get(site - 4)
            if not before or before[1] not in ('add', 'adr'):
                continue
            word, = struct.unpack('<I', image.read(site - 4, 4))
            literal = image.cstring(before[0])
            if word & 31 or not literal or not 1 <= len(literal) <= 256:
                continue
            signature = known_factories[callee], literal
            for pc in range(site + 4, min(site + 36, end), 4):
                word, = struct.unpack('<I', image.read(pc, 4))
                # Only non-x0 address construction and stores of the still
                # unchanged factory result may precede this target store.
                if word & 0x9f000000 == 0x90000000 or word & 0xffc00000 == 0x91000000:
                    if word & 31 == 0:
                        break
                    continue
                if word & 0xffc0001f == 0xf9000000:
                    ref = by_site.get(pc)
                    if ref and ref[1] == 'ldst':
                        section = image.section(ref[0])
                        if section and section['segment'] == '__DATA' and section['name'] in ('__common', '__bss', '__data'):
                            results[signature].append({'address': ref[0], 'call': site, 'store': pc,
                                                       'contract_sha256': hashlib.sha256(image.read(site - 8, pc + 4 - (site - 8))).hexdigest()})
                    continue
                break
        return results
    qualified = []
    for address, row in pairing['functions'].items():
        first, target = int(address, 16), int(row['native'], 16)
        scoped_initializer = row.get('how') == 'string' and factory_evidence and row.get('name') in (
            '__Z17IODeviceTreeAllocPv', '__ZN9IOService10initializeEv', '__Z27IOPlatformActionsInitializev')
        if (row.get('how') != 'reviewed' and not scoped_initializer) or first not in old or target not in new:
            continue
        left = stores(original, first, old[first], old_factories)
        if not left:
            continue
        right = stores(native, target, new[target], native_factories)
        for signature, rows in left.items():
            if len(rows) != 1 or len(right.get(signature, [])) != 1:
                continue
            original_slot, native_slot = rows[0]['address'], right[signature][0]['address']
            if original_slot not in missing:
                continue
            defined = [name for name in symbols.get(native_slot, []) if len(names[name]) == 1]
            if not defined:
                continue
            current = pairing['data'].get(hex(original_slot))
            if current and int(current['native'], 16) != native_slot:
                raise RuntimeError('Actual literal initializer contradicts existing global identity')
            qualified.append({'original': hex(original_slot), 'name': min(defined),
                              'evidence': 'Independently reviewed paired initializer loads same unique literal into x0 immediately before same reviewed intern/plane factory, then stores unchanged return x0 into one actual named global via bounded straight-line address/store instructions; no neighboring BSS inference',
                              'initializer': row['name'], 'factory': signature[0], 'literal': signature[1],
                              'original_contract': {key: hex(value) if isinstance(value, int) else value for key, value in rows[0].items()},
                              'native_contract': {key: hex(value) if isinstance(value, int) else value for key, value in right[signature][0].items()}})
    by_original = defaultdict(list)
    for row in qualified:
        by_original[row['original']].append(row)
    qualified = []
    for address, rows in by_original.items():
        if len({row['native_contract']['address'] for row in rows}) != 1:
            raise RuntimeError('Actual initializer stores disagree on global identity: ' + address)
        row = rows[0]
        row['supporting_initializers'] = sorted({item['initializer'] for item in rows})
        qualified.append(row)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'build': '22G100', 'arch': 'arm64e', 'stock_sha256': sha(args.stock_kernel),
                                   'native_sha256': sha(args.native_kernel), 'observed_at': datetime.now().astimezone().isoformat(),
                                   'functions': factory_evidence, 'data': qualified, 'bootable': False}, indent=2) + '\n')
    print('Concrete interned global initializers qualified', len(qualified), 'and', len(factory_evidence), 'NoCopy factory; import metadata only')


if __name__ == '__main__':
    main()
