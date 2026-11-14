#!/usr/bin/env python3
"""Report unique PAC leaf candidates with differing concrete field offsets.

This is review material only. It emits no import anchors or bindings: same
typed pointer authentication does not prove instance-layout compatibility.
Preserves every raw instruction and both concrete field-offset sequences.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import struct
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'pairing', 'imports', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain old review; choose a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    from fileset_shape import checked_shape
    stock, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(stock.data, stock.entries['com.apple.kernel'][1])
    pairing = json.loads(args.pairing.read_text())
    if Path(pairing['original']).resolve() != args.stock_kernel.resolve() or Path(pairing['native']).resolve() != args.native_kernel.resolve() or pairing.get('build') != '22G100':
        parser.error('Require fresh actual-input pairing')
    imports = json.loads(args.imports.read_text())
    missing = {int(key, 16) for rows in imports['missing'].values() for key in rows}
    groups = defaultdict(lambda: [[], []])
    names = native.symbols()
    for index, image in enumerate((original, native)):
        ranges = [(row['va'], row['va'] + row['filesize']) for row in image.segments
                  if row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST')]
        starts = sorted(a for a in image.function_starts() if any(b <= a < e for b, e in ranges))
        for first, last in zip(starts, starts[1:]):
            if not 16 <= last - first <= 256:
                continue
            code = image.read(first, last - first)
            calls, refs, _ = scan(code, first)
            if refs or any(not first <= target < last for _, target, _ in calls):
                continue
            shape, _, _, _ = checked_shape(code, first)
            if not any(word in (0xdac11a30, 0xdac11230) for word in shape):
                continue
            masked, offsets = [], []
            for slot, word in enumerate(shape):
                if word & 0x3b000000 == 0x39000000:
                    offsets.append({'instruction': slot, 'kind': 'load-store', 'encoded_offset': (word >> 10) & 0xfff})
                    word &= ~(0xfff << 10)
                elif word & 0xff800000 == 0x91000000:
                    offsets.append({'instruction': slot, 'kind': 'add', 'encoded_offset': (word >> 10) & 0xfff})
                    word &= ~(0xfff << 10)
                masked.append(word)
            groups[tuple(masked)][index].append({'address': hex(first), 'names': names.get(first, []) if index else [],
                                               'sha256': hashlib.sha256(code).hexdigest(), 'raw_words': [hex(x) for x in struct.unpack('<' + 'I' * (len(code) // 4), code)],
                                               'field_operands': offsets})
    candidates = []
    for left, right in groups.values():
        if len(left) != 1 or len(right) != 1 or int(left[0]['address'], 16) not in missing or not right[0]['names']:
            continue
        candidates.append({'original': left[0], 'native': right[0], 'binding_authorized': False,
                           'next_check': 'Prove source/API identity and original/source actual field layout; do not ignore offset differences'})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'build': '22G100', 'arch': 'arm64e', 'review_only': True, 'bootable': False,
                                   'stock_sha256': hashlib.sha256(args.stock_kernel.read_bytes()).hexdigest(),
                                   'native_sha256': hashlib.sha256(args.native_kernel.read_bytes()).hexdigest(),
                                   'candidates': candidates}, indent=2) + '\n')
    print('PAC leaf review candidates', len(candidates), '; zero bindings emitted')


if __name__ == '__main__':
    main()
