#!/usr/bin/env python3
"""Exercise the actual import reader's page-straddle/addend refusal boundaries.

Use an actual local ARM64E KDK component. Mutations stay in memory, and output
contains only receipts. This never starts a kernel or accesses a phone.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prelink-kit', type=Path, required=True)
    parser.add_argument('--actual-kext', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Preserve prior evidence; choose a fresh output')
    root = Path(__file__).resolve().parents[3]
    path = root / 'scripts/native18-kext-import-names.py'
    spec = importlib.util.spec_from_file_location('actual_import_reader', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image
    raw = module.arm64e_slice(args.actual_kext.read_bytes())
    image = Image(raw)
    pointers = module.chained_import_fixups(image)
    straddled = [site for site in pointers if site % 4096 == 4092]
    if not straddled:
        parser.error('Choose the actual kext exhibiting a four-byte-aligned page-straddling chain pointer')
    site = straddled[0]
    pointer = pointers[site]
    if not isinstance(pointer[0], str) or pointer[1]:
        parser.error('Expected the actual unauthenticated imported straddling pointer')
    offset = image.offset(site, 8)
    word, = struct.unpack_from('<Q', raw, offset)
    rows = [{'name': 'actual-page-straddling-import', 'result': 'PASS',
             'import': pointer[0], 'site': hex(site)}]
    for addend in (1, -1, 0x3ffff, -0x40000):
        changed = bytearray(raw)
        value = (word & ~(0x7ffff << 32)) | ((addend & 0x7ffff) << 32)
        struct.pack_into('<Q', changed, offset, value)
        found = module.chained_import_fixups(Image(bytes(changed)))[site][0]
        assert found == (pointer[0], addend) and not isinstance(found, (str, int))
        rows.append({'name': 'bind-addend-explicit-' + str(addend), 'result': 'PASS',
                     'API_identity_accepted': False})
    for label, value in (('bad-import-ordinal', (word & ~0xffff) | 0xffff),
                         ('bad-reserved-bind-bits', word | 0x10000),
                         ('stride-outside-page', word | (0x7ff << 51))):
        changed = bytearray(raw)
        struct.pack_into('<Q', changed, offset, value)
        try:
            module.chained_import_fixups(Image(bytes(changed)))
        except ValueError:
            rows.append({'name': label, 'result': 'PASS', 'mutation_refused': True})
        else:
            raise RuntimeError('Invalid actual import metadata accepted: ' + label)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'result': 'PASS', 'controls': rows,
                                    'actual_kext_sha256': hashlib.sha256(args.actual_kext.read_bytes()).hexdigest(),
                                    'reader_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                                    'device_access': False}, indent=2) + '\n')
    print('Actual kext import metadata controls:', len(rows), 'PASS')


if __name__ == '__main__':
    main()
