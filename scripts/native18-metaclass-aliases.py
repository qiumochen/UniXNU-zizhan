#!/usr/bin/env python3
"""Qualify missing constant metaClass pointer aliases of real kernel classes.

Each alias is one compiler-defined pointer to gMetaClass. Both the original
complete class/PAC registration and the independent gMetaClass pairing must
agree. No anonymous neighboring globals or vtable prefixes are inferred.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'class-abi', 'pairing', 'imports', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain old evidence; choose a new output')
    stock_hash, native_hash = sha(args.stock_kernel), sha(args.native_kernel)
    abi = json.loads((args.class_abi / 'receipt.json').read_text())
    if (abi['stock_sha256'], abi['native_sha256']) != (stock_hash, native_hash):
        parser.error('Class ABI receipt belongs to different actual kernels')
    pairing = json.loads(args.pairing.read_text())
    if (pairing.get('build'), pairing.get('arch')) != ('22G100', 'arm64e') or Path(pairing['original']).resolve() != args.stock_kernel.resolve() or Path(pairing['native']).resolve() != args.native_kernel.resolve():
        parser.error('Require fresh pairing for actual kernels')
    imports = json.loads(args.imports.read_text())
    missing = {int(key, 16) for rows in imports['missing'].values() for key in rows}
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image
    stock, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(stock.data, stock.entries['com.apple.kernel'][1])
    left, right = stock.fixups(), native.fixups()
    symbols = {}
    for address, names in native.symbols().items():
        for name in names:
            symbols.setdefault(name, set()).add(address)
    qualified = []
    for row in abi['results']:
        if not row.get('qualified'):
            continue
        cls = row['classname']
        evidence = json.loads((args.class_abi / row['path']).read_text())
        if not evidence.get('abi_compatible') or evidence['classname'] != cls:
            parser.error('Class evidence differs from its qualified receipt')
        meta = int(evidence['metaclass'], 16)
        paired = pairing['data'].get(hex(meta))
        if not paired:
            continue
        alias = '__ZN' + str(len(cls)) + cls + '9metaClassE'
        global_name = '__ZN' + str(len(cls)) + cls + '10gMetaClassE'
        if len(symbols.get(alias, ())) != 1 or len(symbols.get(global_name, ())) != 1:
            continue
        target = next(iter(symbols[alias]))
        native_meta = next(iter(symbols[global_name]))
        if int(paired['native'], 16) != native_meta or right.get(target) != (native_meta, 0, None, None, None):
            continue
        section = native.section(target)
        if not section or section['segment'] not in ('__DATA_CONST', '__AUTH_CONST'):
            continue
        for address in sorted(missing):
            section = original.section(address)
            if not section or section['segment'] not in ('__DATA_CONST', '__AUTH_CONST'):
                continue
            if left.get(address) == (meta, 0, None, None, None):
                qualified.append({'original': hex(address), 'name': alias,
                                  'evidence': 'One const OSMetaClass pointer; complete actual class PAC/registration and independent gMetaClass mapping agree with real named source alias and its complete unauthenticated fixup',
                                  'bytes': 8, 'classname': cls, 'original_pointee': hex(meta),
                                  'native_pointee': hex(native_meta), 'native_alias': hex(target)})
    report = {'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
              'stock_sha256': stock_hash, 'native_sha256': native_hash,
              'pairing_sha256': sha(args.pairing), 'functions': [], 'data': qualified,
              'bootable': False, 'device_access': False}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print('Qualified complete metaClass pointer aliases', len(qualified), '; import evidence only')


if __name__ == '__main__':
    main()
