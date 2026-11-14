#!/usr/bin/env python3
"""Prepare independent raw native OS/SystemOS/Data files and real UUID layout.

Converts only independent machine-local outputs, verifies actual read-only
volume metadata and current OS stage receipts, hashes raw windows and emits
the SHNGROOT directory. No phone access, partition bounds, pinned allocation
or install authority is inferred from another board's layout constants.
"""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import plistlib
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MIB = 1 << 20


def sha(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(8 << 20), b''):
            result.update(block)
    return result.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('candidate', 'systemos', 'catalog-stage', 'catalog', 'metal-stage', 'board-stage', 'pwf-stage', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists() or out.is_relative_to(ROOT):
        parser.error('Use a new machine-local raw output directory')
    priors = {}
    for label, path in (('catalog', args.catalog_stage), ('metal', args.metal_stage), ('board', args.board_stage), ('pwf', args.pwf_stage)):
        receipt = json.loads((path / 'receipt.json').read_text())
        if receipt.get('status') != 'LOCAL_STAGED_READBACK_VERIFIED' or Path(receipt['candidate']).resolve() != args.candidate.resolve() / 'OS.sparseimage':
            parser.error('Require actual current local OS staging receipts: ' + label)
        priors[label] = receipt
    catalog = json.loads((args.catalog / 'receipt.json').read_text())
    if priors['catalog']['catalog_receipt_sha256'] != sha(args.catalog / 'receipt.json'):
        parser.error('Application metadata does not belong to staged catalog')
    inputs = {'OS': args.candidate.resolve() / 'OS.sparseimage',
              'SystemOS': args.systemos.resolve(), 'Data': args.candidate.resolve() / 'Data.sparseimage'}
    if any(not path.is_file() or path.is_symlink() or path.is_relative_to(ROOT) for path in inputs.values()):
        parser.error('All volume inputs must be actual independent local files outside public source')
    out.mkdir(parents=True)
    report = {'started_at': datetime.now().astimezone().isoformat(), 'status': 'RUNNING',
              'build': '22G100', 'board': 'zizhan', 'bootable': False, 'device_access': False,
              'partition_writes': False, 'pinned_file_allocated': False,
              'stage_receipts_sha256': {label: sha(path / 'receipt.json') for label, path in
                                      (('catalog', args.catalog_stage), ('metal', args.metal_stage), ('board', args.board_stage), ('pwf', args.pwf_stage))},
              'volumes': []}
    record = out / 'receipt.json'
    record.write_text(json.dumps(report, indent=2) + '\n')
    invariants = dict(priors['catalog']['binaries'])
    invariants.update(priors['metal']['files'])
    invariants.update({row['path']: row['sha256'] for row in priors['board']['files']})
    invariants['System/Library/xpc/launchd.plist'] = priors['board']['launchd_cache_sha256']
    invariants['pong/firmware/qca6490.pwf'] = priors['pwf']['pack_sha256']
    invariants[catalog['retained_migrator']['path']] = catalog['retained_migrator']['sha256']
    windows, volume_layout, cursor = [], {}, MIB
    try:
        for name, source in inputs.items():
            target = out / (name + '.raw.cdr')
            command = ['hdiutil', 'convert', str(source), '-format', 'UDTO', '-o', str(target)]
            with (out / (name + '-convert.log')).open('w') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            if not target.is_file() or target.samefile(source) or target.stat().st_size % 4096:
                raise RuntimeError('Actual raw volume must be independent and 4KiB-aligned')
            mount = out / (name + '-readonly')
            mount.mkdir()
            attached = plistlib.loads(subprocess.check_output(['hdiutil', 'attach', '-readonly', '-nobrowse', '-owners', 'on', '-mountpoint', str(mount), '-plist', str(target)]))
            disk = attached['system-entities'][0]['dev-entry']
            try:
                info = plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', str(mount)]))
                (out / (name + '-volume.plist')).write_bytes(plistlib.dumps(info))
                if info.get('WritableVolume') is not False or not info.get('GlobalPermissionsEnabled'):
                    raise RuntimeError('Raw output must be genuinely readonly with owners')
                if name in ('OS', 'SystemOS'):
                    version = plistlib.loads((mount / 'System/Library/CoreServices/SystemVersion.plist').read_bytes())
                    if (version.get('ProductVersion'), version.get('ProductBuildVersion')) != ('18.6.2', '22G100'):
                        raise RuntimeError('Actual raw system volume build differs')
                if name == 'OS':
                    for relative, digest in invariants.items():
                        if sha(mount / relative) != digest:
                            raise RuntimeError('Actual OS raw conversion changed verified file: ' + relative)
                    for relative in (*priors['catalog']['binaries'], 'pong/system-app-register', 'pong/wifi-bootstrap'):
                        subprocess.run(['codesign', '--verify', '--strict', str(mount / relative)], check=True)
                    subprocess.run(['codesign', '--verify', '--strict', str(mount / 'System/Library/Extensions/PongAdrenoMetal.bundle')], check=True)
                if name == 'Data':
                    entries = [path.name for path in mount.iterdir()]
                    if entries:
                        raise RuntimeError('Isolated Data must remain truly blank before native bootstrap: ' + repr(entries))
                uuid = info['VolumeUUID']
            finally:
                subprocess.run(['hdiutil', 'detach', disk], check=True)
            if name == 'Data':
                cursor = (cursor + 16 * MIB + MIB - 1) // MIB * MIB
            size = target.stat().st_size
            digest = sha(target)
            row = {'name': name, 'image': str(target), 'offset': cursor, 'size': size,
                   'block_bytes': 512, 'sha256': digest, 'volume_uuid': uuid}
            windows.append(row)
            volume_layout[{'OS': 'os', 'SystemOS': 'systemos', 'Data': 'data'}[name]] = {'uuid': uuid}
            report['volumes'].append(row)
            record.write_text(json.dumps(report, indent=2) + '\n')
            cursor = (cursor + size + MIB - 1) // MIB * MIB
        if len({row['volume_uuid'] for row in windows}) != 3:
            raise RuntimeError('Independent volume UUIDs must not collide with one another')
        header = bytearray(4096)
        header[:8] = b'SHNGROOT'
        struct.pack_into('<IIQ', header, 8, 1, 3, cursor)
        for index, row in enumerate(windows):
            struct.pack_into('<32sQQII32s40s', header, 24 + index * 128, row['name'].encode(), row['offset'], row['size'],
                             512, 1, bytes.fromhex(row['sha256']), bytes(40))
        header[4064:] = hashlib.sha256(header[:4064]).digest()
        (out / 'shngroot-native18.bin').write_bytes(header)
        volume_layout.update(build='22G100', board='zizhan', scope='real independent raw candidate UUIDs; no device placement/install')
        (out / 'volume-layout.json').write_text(json.dumps(volume_layout, indent=2) + '\n')
        layout = {'build': '22G100', 'board': 'zizhan', 'image_bytes': cursor, 'windows': windows,
                  'bootable': False, 'partition_writes': False, 'pinned_file_allocated': False,
                  'pending': ['complete stock-kext imports', 'real device extent allocation/backup/return preflight', 'temporary boot qualification']}
        (out / 'ufs-layout.json').write_text(json.dumps(layout, indent=2) + '\n')
        report.update(status='RAW_LOCAL_VOLUMES_VERIFIED', image_bytes=cursor,
                      directory_sha256=sha(out / 'shngroot-native18.bin'), volume_layout_sha256=sha(out / 'volume-layout.json'),
                      isolated_data_blank=True)
    except BaseException as error:
        report.update(status='FAIL', reason=str(error))
        raise
    finally:
        report['finished_at'] = datetime.now().astimezone().isoformat()
        record.write_text(json.dumps(report, indent=2) + '\n')
    print('Independent native raw OS/SystemOS/blank Data verified; real UUID layout; no phone allocation/write')


if __name__ == '__main__':
    main()
