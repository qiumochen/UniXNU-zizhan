#!/usr/bin/env python3
"""Inventory same-XNU Apple KDK naming leads without authorizing bindings.

Reads a symbol-bearing kernel and its UUID-matched dSYM. Complete normalized
instruction bodies must match uniquely in both images, including available
strings. Relocated call/data operands still need independent proof; these
research names deliberately cannot enter native18-closure's exact-name input.
Apple inputs, addresses and the output stay in the private work tree.
"""
import argparse
from collections import defaultdict
from datetime import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
PROFILE = tomllib.loads((ROOT / 'configs/zizhan-ios18e-inputs.toml').read_text())


def metadata(blob):
    """Read UUID and defined symbols without assuming a dSYM has fileoff zero."""
    if len(blob) < 32:
        raise ValueError('Truncated Mach-O header')
    magic, cpu, subtype, kind, count, size = struct.unpack_from('<6I', blob)
    if magic != 0xfeedfacf or cpu != 0x100000c or 32 + size > len(blob):
        raise ValueError('Require a complete ARM64 Mach-O')
    offset, uuid, symtab = 32, None, None
    for _ in range(count):
        if offset + 8 > 32 + size:
            raise ValueError('Truncated load command')
        command, length = struct.unpack_from('<II', blob, offset)
        if length < 8 or offset + length > 32 + size:
            raise ValueError('Invalid load-command length')
        if command == 0x1b:
            if length != 24 or uuid is not None:
                raise ValueError('Ambiguous UUID command')
            uuid = blob[offset + 8:offset + 24].hex()
        elif command == 2:
            if length != 24 or symtab is not None:
                raise ValueError('Ambiguous symbol-table command')
            symtab = struct.unpack_from('<4I', blob, offset + 8)
        offset += length
    if offset != 32 + size or uuid is None or symtab is None:
        raise ValueError('Require one complete UUID and symbol table')
    symoff, nsyms, stroff, strsize = symtab
    if symoff + 16 * nsyms > len(blob) or stroff + strsize > len(blob):
        raise ValueError('Symbol table overruns file')
    symbols = defaultdict(set)
    for index in range(nsyms):
        strx, ntype, _, _, address = struct.unpack_from('<IBBHQ', blob, symoff + 16 * index)
        if ntype & 0xe0 or ntype & 0x0e != 0x0e:
            continue
        if strx >= strsize:
            raise ValueError('Symbol name overruns string table')
        end = blob.find(b'\0', stroff + strx, stroff + strsize)
        if end < 0:
            raise ValueError('Unterminated symbol name')
        symbols[address].add(blob[stroff + strx:end].decode())
    return {'uuid': uuid, 'cpu': cpu, 'subtype': subtype, 'kind': kind,
            'nsyms': nsyms, 'symbols': symbols}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('native-source', 'prelink-kit', 'stock-kernel', 'reference-kernel',
                 'reference-dwarf', 'imports', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists() or args.out.resolve() == ROOT or ROOT in args.out.resolve().parents:
        parser.error('Use a new private output path')
    for path, pin in ((args.native_source, PROFILE['source']['commit']),
                      (args.prelink_kit, PROFILE['prelink']['commit'])):
        if subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip() != pin:
            parser.error('Wrong pinned analysis source')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    path = args.native_source / 'scripts/derive-pong-pac-shell-names.py'
    spec = importlib.util.spec_from_file_location('pinned_pac_shell_reference', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    blobs = [path.read_bytes() for path in (args.stock_kernel, args.reference_kernel, args.reference_dwarf)]
    kernel_meta, dwarf_meta = (metadata(blob) for blob in blobs[1:])
    if (kernel_meta['kind'], dwarf_meta['kind']) != (2, 10) or any(kernel_meta[key] != dwarf_meta[key] for key in ('uuid', 'cpu', 'subtype')):
        parser.error('Reference kernel/dSYM identity mismatch')
    versions = [re.search(rb'Darwin Kernel Version[^\0]{1,220}', blob) for blob in blobs[:2]]
    if any(value is None or b'xnu-11417.140.69~' not in value.group() for value in versions):
        parser.error('Both actual kernels must have the target XNU version')
    cache, reference = Image(blobs[0]), Image(blobs[1])
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    if not kernel_meta['symbols'].keys() <= dwarf_meta['symbols'].keys():
        parser.error('dSYM does not cover exported kernel symbol addresses')
    for address, names in kernel_meta['symbols'].items():
        if not names <= dwarf_meta['symbols'][address]:
            parser.error('dSYM contradicts exported kernel symbols')
    name_addresses = defaultdict(set)
    for address, names in dwarf_meta['symbols'].items():
        for name in names:
            name_addresses[name].add(address)
    groups = []
    for image in (original, reference):
        ranges = [(s['va'], s['va'] + s['filesize']) for s in image.segments
                  if s['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and s['filesize']]
        starts = sorted(address for address in image.function_starts() if any(lo <= address < hi for lo, hi in ranges))
        values = defaultdict(list)
        for index, start in enumerate(starts):
            end = min([hi for lo, hi in ranges if lo <= start < hi] + ([starts[index + 1]] if index + 1 < len(starts) else []))
            value = module.shell(image, start, end)
            if value is not None:
                shape, strings, _ = value
                calls, refs, _ = scan(image.read(start, end - start), start)
                values[shape].append((start, strings, len(calls), len(refs)))
        groups.append(values)
    coverage = json.loads(args.imports.read_text())
    if coverage.get('qualification') != 'INDEPENDENT_CONTRACTS_ONLY' or coverage.get('build') != '22G100':
        parser.error('Require current independent import inventory')
    missing = {address for rows in coverage['missing'].values() for address in rows}
    rows = []
    for shape, left in groups[0].items():
        right = groups[1].get(shape, [])
        if len(left) != 1 or len(right) != 1 or left[0][1] != right[0][1]:
            continue
        address, _, calls, refs = left[0]
        target = right[0][0]
        names = sorted(name for name in dwarf_meta['symbols'].get(target, ()) if len(name_addresses[name]) == 1
                       and not any(part in name for part in ('.cold.', '.llvm.', 'OUTLINED_FUNCTION')))
        if names:
            rows.append({'original': hex(address), 'reference_address': hex(target), 'names': names,
                         'instructions': len(shape), 'stock_calls': calls, 'stock_data_references': refs,
                         'currently_unqualified_import': hex(address) in missing,
                         'binding_authorized': False})
    rows.sort(key=lambda row: int(row['original'], 16))
    report = {'observed_at': datetime.now().astimezone().isoformat(), 'build': '22G100',
              'reference_role': 'names-only-research', 'analysis_only': True, 'bootable': False,
              'binding_authorized': False, 'device_access': False,
              'versions': [value.group().decode() for value in versions],
              'reference_uuid': kernel_meta['uuid'], 'reference_defined_addresses': len(dwarf_meta['symbols']),
              'inputs': [{'path': str(path.resolve()), 'sha256': hashlib.sha256(blob).hexdigest()}
                         for path, blob in zip((args.stock_kernel, args.reference_kernel, args.reference_dwarf), blobs)],
              'imports_sha256': hashlib.sha256(args.imports.read_bytes()).hexdigest(),
              'normalizer_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
              'policy': 'Unique complete PAC-shell instruction shapes and equal string sets are naming leads only; every relocated operand/API contract remains to prove',
              'functions': rows, 'matched': len(rows),
              'currently_unqualified_matches': sum(row['currently_unqualified_import'] for row in rows)}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print('Research naming leads:', report['matched'], '; current missing-import leads:', report['currently_unqualified_matches'], '; zero bindings authorized')


if __name__ == '__main__':
    main()
