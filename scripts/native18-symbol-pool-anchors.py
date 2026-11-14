#!/usr/bin/env python3
"""Review the actual OSSymbolPool findSymbol API boundary.

The stock NoCopy caller is identified from its real bounded-strnlen call,
pool literal, key arguments, authenticated OSSymbol allocation, NoCopy fields
and insertion edge. The source caller and full source method are checked
separately. UUID-matched same-XNU KDK symbols corroborate names only; KDK
addresses are never used. This is a final-import contract, not proof of SMR
concurrency, allocator behavior, PAC keys or device lifetime.
"""
# Historical proof was withdrawn: preserve replay research, never authorize imports.
# See docs/progress/2026-11-14-native-symbol-pool-correction.md.
import argparse
from datetime import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
PROFILE = tomllib.loads((ROOT / 'configs/zizhan-ios18e-inputs.toml').read_text())
STOCK_STRNLEN = 0xfffffff008106ac0
STOCK_FINDSYMBOL = 0xfffffff008797098
SOURCE_FIND = '__ZNK12OSSymbolPool10findSymbolE10smrh_key_t'
FACTORY = '__ZN8OSSymbol17withCStringNoCopyEPKc'
KDK_FACTORY = '__ZN8OSSymbol17withCStringNoCopyEPKc'
KDK_FIND = '__ZNK12OSSymbolPool10findSymbolE10smrh_key_t'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def function_bodies(image):
    ranges = [(row['va'], row['va'] + row['filesize']) for row in image.segments
              if row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['filesize']]
    starts = sorted(address for address in image.function_starts()
                    if any(first <= address < last for first, last in ranges))
    return {first: image.read(first, last - first) for first, last in zip(starts, starts[1:])
            if 32 <= last - first <= 65536 and any(lo <= first and last <= hi for lo, hi in ranges)}


def body(image, address):
    end = min(value for value in image.function_starts() if value > address)
    return image.read(address, end - address)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('native-source', 'prelink-kit', 'stock-kernel', 'native-kernel',
                 'native-build', 'factory-pairing', 'class-abi', 'kdk-kernel', 'kdk-dwarf',
                 'strnlen-anchor', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if subprocess.check_output(['git', '-C', str(args.native_source), 'rev-parse', 'HEAD'], text=True).strip() != PROFILE['source']['commit']:
        parser.error('Wrong pinned source revision')
    if subprocess.check_output(['git', '-C', str(args.prelink_kit), 'rev-parse', 'HEAD'], text=True).strip() != PROFILE['prelink']['commit']:
        parser.error('Wrong pinned prelink revision')
    if args.out.exists() or args.out.resolve() == ROOT or ROOT in args.out.resolve().parents:
        parser.error('Use a new private output path')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    stock_bytes, native_bytes = args.stock_kernel.read_bytes(), args.native_kernel.read_bytes()
    cache, native = Image(stock_bytes), Image(native_bytes)
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    built = json.loads((args.native_build / 'host-core.json').read_text())
    if built.get('returncode') != 0 or not any(Path(row['path']).name == args.native_kernel.name and row['sha256'] == sha(args.native_kernel) for row in built['artifacts']):
        parser.error('Require completed actual native image')
    class_receipt = json.loads((args.class_abi / 'receipt.json').read_text())
    if (class_receipt.get('stock_sha256'), class_receipt.get('native_sha256')) != (sha(args.stock_kernel), sha(args.native_kernel)):
        parser.error('Require fresh OSSymbol class evidence for actual images')
    symbol_class = json.loads((args.class_abi / 'OSSymbol.json').read_text())
    if not symbol_class.get('abi_compatible'):
        parser.error('OSSymbol source/stock class ABI is not qualified')
    strnlen = json.loads(args.strnlen_anchor.read_text())
    if (strnlen.get('stock_sha256'), strnlen.get('native_sha256')) != (sha(args.stock_kernel), sha(args.native_kernel)) or strnlen.get('replay', {}).get('result') != 'PASS':
        parser.error('Require current actual-input bounded strnlen proof')
    candidate = json.loads(args.factory_pairing.read_text())
    if candidate.get('qualification') != 'INDEPENDENT_CONTRACTS_ONLY' or Path(candidate['original']).resolve() != args.stock_kernel.resolve() or Path(candidate['native']).resolve() != args.native_kernel.resolve():
        parser.error('Require current independently qualified actual-input pairing')
    names = {}
    for address, values in native.symbols().items():
        for name in values:
            names.setdefault(name, set()).add(address)
    def named(name):
        if len(names.get(name, ())) != 1:
            raise RuntimeError('Actual source definition not unique: ' + name)
        return next(iter(names[name]))
    stock_bodies, source_bodies = function_bodies(original), function_bodies(native)
    # Stock NoCopy has a distinctive complete sequence: bounded strnlen,
    # 0x3fffe cap, (pool,string,length) lookup arguments, OSSymbol allocation,
    # NoCopy/length fields, authenticated string store, then pool insertion.
    factories = []
    for address, code in stock_bodies.items():
        calls, refs, shape = scan(code, address)
        str_calls = [(site, target) for site, target, link in calls if link and target == STOCK_STRNLEN]
        find_calls = [(site, target) for site, target, link in calls if link and target == STOCK_FINDSYMBOL]
        if len(str_calls) != 1 or len(find_calls) != 1:
            continue
        call = find_calls[0][0]
        offset = call - address
        words = struct.unpack('<' + 'I' * (len(code) // 4), code)
        if offset < 12 or (words[offset // 4 - 2:offset // 4] != (0xaa1303e1, 0xaa1403e2)):
            continue
        if not all(struct.pack('<I', word) in code for word in
                   (0x52800028, 0x2a163908, 0xb9000c08, 0xf2e3e1d0)):
            # Require the NoCopy flag/length and string-pointer discriminator,
            # not merely the shared copying-factory lookup prefix.
            continue
        factories.append((address, code, calls, refs))
    if len(factories) != 1:
        raise RuntimeError('Actual stock NoCopy caller/findSymbol target is ambiguous')
    stock_factory, stock_factory_code, stock_calls, stock_refs = factories[0]
    source_factory = named(FACTORY)
    source_code = source_bodies.get(source_factory)
    if source_code is None:
        raise RuntimeError('Actual source NoCopy factory body is unavailable')
    _, source_refs, _ = scan(source_code, source_factory)
    stock_find_site = next(site for site, target, link in stock_calls if link and target == STOCK_FINDSYMBOL)
    source_calls, _, _ = scan(source_code, source_factory)
    source_find_site = next((site for site, target, link in source_calls if link and target == named(SOURCE_FIND)), None)
    if source_find_site is None:
        raise RuntimeError('Actual source NoCopy does not call its unique findSymbol definition')
    def args_match(code, base, site):
        words = struct.unpack('<' + 'I' * (len(code) // 4), code)
        index = (site - base) // 4
        return (index >= 3 and words[index - 2:index] == (0xaa1303e1, 0xaa1403e2) and
                any(ref_site == site - 12 and kind == 'add' for ref_site, _, kind in scan(code, base)[1]))
    if not args_match(stock_factory_code, stock_factory, stock_find_site):
        raise RuntimeError('Stock findSymbol key arguments are not the real NoCopy string/length pair')
    if not args_match(source_code, source_factory, source_find_site):
        raise RuntimeError('Source findSymbol key arguments are not the real NoCopy string/length pair')
    # Same-XNU KDK is a corroborating symbol-name source only. Verify its own
    # kernel/dSYM UUID and name definition, but never transfer its address.
    helper = Path(__file__).with_name('native18-kdk-reference.py')
    spec = importlib.util.spec_from_file_location('kdk_symbol_metadata', helper)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    kdk_kernel = module.metadata(args.kdk_kernel.read_bytes())
    kdk_dwarf = module.metadata(args.kdk_dwarf.read_bytes())
    if (kdk_kernel['uuid'], kdk_kernel['cpu'], kdk_kernel['subtype']) != (kdk_dwarf['uuid'], kdk_dwarf['cpu'], kdk_dwarf['subtype']):
        parser.error('KDK kernel/dSYM UUID mismatch')
    if not any(KDK_FIND in values for values in kdk_dwarf['symbols'].values()) or not any(KDK_FACTORY in values for values in kdk_dwarf['symbols'].values()):
        parser.error('Same-XNU KDK/dSYM does not corroborate both API names')
    source_text = (ROOT / 'refer/ipados18/xnu-11417.140.69/libkern/c++/OSSymbol.cpp').read_text()
    required = ('OSSymbolPool::findSymbol(smrh_key_t key)', 'smrh_key_t key = {', '.smrk_string = cString,', '.smrk_len    = strnlen(cString, kMaxStringLength)', 'auto symbol = pool.findSymbol(key);', 'return symbol;')
    if any(token not in source_text for token in required):
        raise RuntimeError('Pinned source no longer states the reviewed NoCopy/key/find/return contract')
    # Exact native source dependencies are enumerated from actual branch/call
    # operands. No pool/hash/allocator/smr address is inferred or faked.
    find_address = named(SOURCE_FIND)
    find_code = source_bodies[find_address]
    find_calls, find_refs, _ = scan(find_code, find_address)
    if len(find_calls) < 10 or not find_refs:
        raise RuntimeError('Actual findSymbol body/dependency surface unexpectedly changed')
    dependencies = []
    for site, target, link in find_calls:
        if not link or find_address <= target < find_address + len(find_code):
            continue
        stock_match = candidate['functions'].get(hex(STOCK_FINDSYMBOL))
        dependencies.append({'site': hex(site), 'source_target': hex(target),
                             'unique_source_names': sorted(native.symbols().get(target, [])),
                             'independently_qualified_stock_target': bool(stock_match and int(stock_match['native'], 16) == target)})
    report = {'review_only': True, 'binding_authorized': False,
              'qualification': 'REJECTED_HISTORICAL_PROOF', 'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
              'stock_sha256': sha(args.stock_kernel), 'native_sha256': sha(args.native_kernel),
              'functions': [{'original': hex(STOCK_FINDSYMBOL), 'name': SOURCE_FIND, 'final_import_only': True,
                             'evidence': 'Actual unique stock OSSymbol NoCopy caller passes its real bounded string and length to this stock target; actual source NoCopy caller passes the same 16-byte smrh_key_t split across x1/x2 to the unique source method; pinned XNU source confirms key fields, hash/equality semantics, bounded small/large key paths and retained/null return contract; UUID-matched same-XNU Apple KDK dSYM corroborates both API names only. Complete SMR/allocator/lifetime/hardware behavior remains unqualified',
                             'stock_factory': hex(stock_factory), 'source_factory': hex(source_factory),
                             'native_target': hex(find_address), 'original_sha256': hashlib.sha256(body(original, STOCK_FINDSYMBOL)).hexdigest(),
                             'native_sha256': hashlib.sha256(find_code).hexdigest()}],
              'data': [], 'bootable': False, 'device_access': False,
              'operational_dependencies': dependencies,
              'api_contract': {'key_layout': {'string_pointer_register': 'x1', 'length_register': 'x2', 'source_type': 'smrh_key_t', 'string_field_offset': 0, 'length_field_offset': 8, 'size': 16},
                               'result': 'retained matching OSSymbol or null', 'matching': 'byte length and exact bytes',
                               'temporary_copy': 'source copies potentially pageable key before SMR lookup; <=128 bytes stack, larger key temporary allocation',
                               'large_key_path': 'serialized huge-symbol list protected by pool lock',
                               'retained_object_path': 'permanent flag or actual tagged try-retain',
                               'complete_SMR_concurrency_and_allocator_qualified': False,
                               'KDK_addresses_transferred': False},
              'kdk_uuid': kdk_kernel['uuid'], 'kdk_name_role': 'names-only corroboration',
              'scope': 'KPI call contract only; no scheduler/allocator/SMR success model and no device authentication'}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print('Actual NoCopy/findSymbol API boundary reviewed; full SMR/allocator dependencies remain separate')


if __name__ == '__main__':
    main()
