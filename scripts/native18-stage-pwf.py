#!/usr/bin/env python3
"""Stage the verified on-demand Wi-Fi pack in the current independent OS.

Requires actual external-reader build, sanitizer replay, connected-device
firmware hash comparison and completed candidate board/catalog/Metal state.
Only the new PWF2 data file changes; no phone/Data or launchd-cache writes.
"""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('candidate', 'restore-os', 'wifi-build', 'reader-test', 'board-stage', 'catalog-stage', 'catalog', 'metal-stage', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if os.geteuid():
        parser.error('Actual local native ownership staging requires root')
    out = args.out.resolve()
    image = args.candidate.resolve() / 'OS.sparseimage'
    if out.exists() or out.is_relative_to(ROOT) or not image.is_file() or image.is_symlink() or image.is_relative_to(ROOT) or image.samefile(args.restore_os):
        parser.error('Use independent local OS and new operation output')
    wifi = json.loads((args.wifi_build / 'receipt.json').read_text())
    test = json.loads((args.reader_test / 'results.json').read_text())
    device_check = json.loads((args.wifi_build / 'device-firmware-hash-check.json').read_text())
    pack = Path(wifi['firmware_pack']['path'])
    if wifi.get('returncode') != 0 or not wifi.get('external_firmware') or test.get('result') != 'PASS' or test.get('source_sha256') != wifi['external_firmware']['reader_source_sha256'] or test.get('pack_sha256') != sha(pack) or sha(pack) != wifi['firmware_pack']['sha256']:
        parser.error('Require actual matching external reader/pack and sanitizer replay')
    if device_check.get('result') != 'PASS' or device_check.get('phone_written') is not False or device_check.get('checked') != len(wifi['firmware']) or {row['name'] for row in device_check['files']} != {row['name'] for row in wifi['firmware']} or not all(row['matches'] for row in device_check['files']):
        parser.error('Require all actual input files to match connected zizhan firmware hashes')
    board = json.loads((args.board_stage / 'receipt.json').read_text())
    catalog_stage = json.loads((args.catalog_stage / 'receipt.json').read_text())
    catalog = json.loads((args.catalog / 'receipt.json').read_text())
    metal = json.loads((args.metal_stage / 'receipt.json').read_text())
    for prior in (board, catalog_stage, metal):
        if prior.get('status') != 'LOCAL_STAGED_READBACK_VERIFIED' or Path(prior['candidate']).resolve() != image:
            parser.error('Require actual completed current candidate stage receipts')
    out.mkdir(parents=True)
    mount = out / 'os-mount'
    mount.mkdir()
    report = {'started_at': datetime.now().astimezone().isoformat(), 'status': 'RUNNING', 'candidate': str(image),
              'build': '22G100', 'pack_sha256': sha(pack), 'pack_bytes': pack.stat().st_size,
              'wifi_receipt_sha256': sha(args.wifi_build / 'receipt.json'),
              'reader_test_sha256': sha(args.reader_test / 'results.json'),
              'device_hash_receipt_sha256': sha(args.wifi_build / 'device-firmware-hash-check.json'),
              'phone_written': False, 'data_image_modified': False, 'bootable': False}
    record = out / 'receipt.json'
    record.write_text(json.dumps(report, indent=2) + '\n')
    relative = 'pong/firmware/qca6490.pwf'
    invariants = dict(catalog_stage['binaries'])
    invariants.update(metal['files'])
    invariants.update({row['path']: row['sha256'] for row in board['files']})
    invariants['System/Library/xpc/launchd.plist'] = board['launchd_cache_sha256']
    invariants[catalog['retained_migrator']['path']] = catalog['retained_migrator']['sha256']
    try:
        for readonly in (False, True):
            attached = plistlib.loads(subprocess.check_output(['hdiutil', 'attach', *(['-readonly'] if readonly else []), '-nobrowse', '-owners', 'on', '-mountpoint', str(mount), '-plist', str(image)]))
            disk = attached['system-entities'][0]['dev-entry']
            try:
                info = plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', str(mount)]))
                version = plistlib.loads((mount / 'System/Library/CoreServices/SystemVersion.plist').read_bytes())
                if info.get('WritableVolume') != (not readonly) or not info.get('GlobalPermissionsEnabled') or version.get('ProductBuildVersion') != '22G100':
                    raise RuntimeError('Actual native candidate mount/access differs')
                for path, digest in invariants.items():
                    if sha(mount / path) != digest:
                        raise RuntimeError('Previously verified candidate invariant changed: ' + path)
                target = mount / relative
                if not readonly:
                    if info.get('APFSContainerFree', 0) < pack.stat().st_size + (256 << 20):
                        raise RuntimeError('Local APFS capacity prerequisite failed before writes')
                    if os.path.lexists(target):
                        raise RuntimeError('Retain existing Wi-Fi pack')
                    for parent in target.parents:
                        if parent == mount:
                            break
                        if parent.is_symlink():
                            raise RuntimeError('Unexpected firmware target parent symlink')
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(pack, target)
                    os.chown(target, 0, 0)
                    target.chmod(0o644)
                metadata = target.lstat()
                if target.is_symlink() or sha(target) != report['pack_sha256'] or metadata.st_uid or metadata.st_gid or metadata.st_mode & 0o777 != 0o644:
                    raise RuntimeError('Actual staged Wi-Fi pack hash/ownership/mode differs')
            finally:
                subprocess.run(['hdiutil', 'detach', disk], check=True)
        report.update(status='LOCAL_STAGED_READBACK_VERIFIED', readonly_readback=True, path='/' + relative,
                      previous_invariants_unchanged=True, firmware_executed=False)
    except BaseException as error:
        report.update(status='FAIL', reason=str(error))
        raise
    finally:
        report['finished_at'] = datetime.now().astimezone().isoformat()
        record.write_text(json.dumps(report, indent=2) + '\n')
    print('Actual external PWF2 pack locally staged/read back; no firmware execution or phone/Data write')


if __name__ == '__main__':
    main()
