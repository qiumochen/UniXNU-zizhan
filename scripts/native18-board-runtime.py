#!/usr/bin/env python3
"""Build or locally stage scoped native Wi-Fi bootstrap and ADSP firmware.

Uses the native reference's real SC/MobileWiFi worker and the connected
zizhan's hash-verified firmware. No inherited foreign audio/modem policy is
selected. apply checks the current isolated OS/catalog/Metal invariants and
independently reads all new files back; never contacts or writes the phone.
"""
import argparse
from datetime import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('build', 'apply'))
    for name in ('native-source', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('firmware', 'runtime', 'candidate', 'restore-os', 'catalog-stage', 'catalog', 'metal-stage'):
        parser.add_argument('--' + name, type=Path)
    args = parser.parse_args()
    source, out = args.native_source.resolve(), args.out.resolve()
    pin = tomllib.loads((ROOT / 'configs/zizhan-ios18e-inputs.toml').read_text())['source']
    if subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() != pin['commit']:
        parser.error('Wrong native source revision')
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error('Use a new machine-local operation directory')
    required = ('firmware',) if args.stage == 'build' else ('runtime', 'candidate', 'restore_os', 'catalog_stage', 'catalog', 'metal_stage')
    if any(getattr(args, key) is None for key in required):
        parser.error('Stage requires ' + ', '.join(required))
    report = {'started_at': datetime.now().astimezone().isoformat(), 'source': pin['commit'],
              'build': '22G100', 'arch': 'arm64e', 'stage': args.stage, 'status': 'RUNNING',
              'phone_written': False, 'data_image_modified': False, 'bootable': False}
    out.mkdir(parents=True)
    record = out / 'receipt.json'
    record.write_text(json.dumps(report, indent=2) + '\n')
    if args.stage == 'build':
        fw = json.loads((args.firmware / 'receipt.json').read_text())
        binary = args.firmware / 'adsp.mbn'
        if fw.get('phone_written') is not False or not fw.get('joined', {}).get('all_program_file_bytes_verified') or fw['joined']['sha256'] != sha(binary) or fw['joined']['load_span'] != 0x3100000 or not all(row.get('device_readback_hash_match') for row in fw['files']):
            parser.error('Require the actual read-only device firmware receipt and complete joined program bytes')
        sys.path[:0] = [str(source / 'scripts'), str(source / 'userland')]
        spec = importlib.util.spec_from_file_location('zizhan_native_board_wifi', source / 'userland/board_services18.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        overlay = out / 'overlay'
        runtime = out / 'build'
        module.build_wifi(runtime)
        target = overlay / 'pong/wifi-bootstrap'
        target.parent.mkdir(parents=True)
        shutil.copy2(runtime / 'wifi-bootstrap', target)
        raw = target.read_bytes()
        if struct.unpack_from('<III', raw) != (0xfeedfacf, 0x100000c, 0x80000002):
            parser.error('Wi-Fi runtime must be actual user arm64e')
        subprocess.run(['codesign', '--verify', '--strict', str(target)], check=True)
        signature = subprocess.run(['codesign', '-d', '--verbose=4', str(target)], capture_output=True, text=True, check=True)
        if 'Page size=4096' not in signature.stderr:
            parser.error('Wi-Fi runtime signature pages differ')
        (out / 'wifi-bootstrap.signature.txt').write_text(signature.stderr)
        firmware = overlay / 'pong/firmware/adsp.mbn'
        firmware.parent.mkdir()
        shutil.copy2(binary, firmware)
        firmware.chmod(0o644)
        label = 'dev.unixnu.zizhan.wifi-bootstrap'
        job = {'Label': label, 'ProgramArguments': ['/pong/wifi-bootstrap'], 'UserName': 'root', 'RunAtLoad': True,
               'LaunchOnlyOnce': True, 'ProcessType': 'Background', 'ExitTimeOut': 3,
               'StandardOutPath': '/dev/console', 'StandardErrorPath': '/dev/console'}
        relative = 'System/Library/LaunchDaemons/' + label + '.plist'
        path = overlay / relative
        path.parent.mkdir(parents=True)
        path.write_bytes(plistlib.dumps(job, fmt=plistlib.FMT_BINARY))
        path.chmod(0o644)
        files = [{'path': str(path.relative_to(overlay)), 'sha256': sha(path), 'bytes': path.stat().st_size,
                  'mode': 0o755 if path.name == 'wifi-bootstrap' else 0o644} for path in overlay.rglob('*') if path.is_file()]
        report.update(status='HOST_VERIFIED', files=files, job_path=relative, job=job,
                      firmware_receipt_sha256=sha(args.firmware / 'receipt.json'),
                      selected='native real-interface SC/MobileWiFi worker; external matching ADSP firmware; no audio/modem policy',
                      runtime_qualified=False, finished_at=datetime.now().astimezone().isoformat())
        record.write_text(json.dumps(report, indent=2) + '\n')
        print('HOST native Wi-Fi bootstrap and verified ADSP firmware overlay prepared; no phone/image writes')
        return
    if os.geteuid():
        parser.error('Local OS staging requires root ownership')
    runtime = json.loads((args.runtime / 'receipt.json').read_text())
    prior = json.loads((args.catalog_stage / 'receipt.json').read_text())
    metal = json.loads((args.metal_stage / 'receipt.json').read_text())
    catalog = json.loads((args.catalog / 'receipt.json').read_text())
    image = args.candidate.resolve() / 'OS.sparseimage'
    if image.is_symlink() or not image.is_file() or image.samefile(args.restore_os) or image.is_relative_to(ROOT):
        parser.error('Require independent local candidate OS sparseimage')
    if runtime.get('status') != 'HOST_VERIFIED' or runtime.get('stage') != 'build' or prior.get('status') != 'LOCAL_STAGED_READBACK_VERIFIED' or Path(prior['candidate']).resolve() != image or metal.get('status') != 'LOCAL_STAGED_READBACK_VERIFIED':
        parser.error('Require the actual completed runtime and candidate catalog/Metal receipts')
    if prior['catalog_receipt_sha256'] != sha(args.catalog / 'receipt.json'):
        parser.error('Catalog operation differs from current prepared overlay')
    for row in runtime['files']:
        path = args.runtime / 'overlay' / row['path']
        if path.is_symlink() or sha(path) != row['sha256']:
            parser.error('Board runtime source changed')
    expected_cache = args.catalog / 'overlay/System/Library/xpc/launchd.plist'
    baseline_cache_sha = sha(expected_cache)
    cache = plistlib.loads(expected_cache.read_bytes())
    if '/' + runtime['job_path'] in cache['LaunchDaemons']:
        parser.error('Retain previous board runtime job')
    cache['LaunchDaemons']['/' + runtime['job_path']] = runtime['job']
    cache_bytes = plistlib.dumps(cache, fmt=plistlib.FMT_BINARY)
    cache_sha = hashlib.sha256(cache_bytes).hexdigest()
    mount = out / 'os-mount'
    mount.mkdir()
    report.update(candidate=str(image), runtime_receipt_sha256=sha(args.runtime / 'receipt.json'),
                  prior_catalog_receipt_sha256=sha(args.catalog_stage / 'receipt.json'))
    record.write_text(json.dumps(report, indent=2) + '\n')
    try:
        for readonly in (False, True):
            attached = plistlib.loads(subprocess.check_output(['hdiutil', 'attach', *(['-readonly'] if readonly else []), '-nobrowse', '-owners', 'on', '-mountpoint', str(mount), '-plist', str(image)]))
            disk = attached['system-entities'][0]['dev-entry']
            try:
                volume = plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', str(mount)]))
                version = plistlib.loads((mount / 'System/Library/CoreServices/SystemVersion.plist').read_bytes())
                if volume.get('WritableVolume') != (not readonly) or not volume.get('GlobalPermissionsEnabled') or version.get('ProductBuildVersion') != '22G100':
                    raise RuntimeError('Actual candidate mount/version differs')
                if not readonly and volume.get('APFSContainerFree', 0) < sum(row['bytes'] for row in runtime['files']) + (256 << 20):
                    raise RuntimeError('Local APFS capacity prerequisite failed before writes')
                for relative, digest in prior['binaries'].items():
                    if sha(mount / relative) != digest:
                        raise RuntimeError('Prior native bootstrap changed')
                for relative, digest in metal['files'].items():
                    if sha(mount / relative) != digest:
                        raise RuntimeError('Prior native Metal changed')
                if sha(mount / catalog['retained_migrator']['path']) != catalog['retained_migrator']['sha256']:
                    raise RuntimeError('Native removable-app migrator changed')
                cache_path = mount / 'System/Library/xpc/launchd.plist'
                if sha(cache_path) != (cache_sha if readonly else baseline_cache_sha):
                    raise RuntimeError('Actual native launchd cache differs')
                if not readonly:
                    for row in runtime['files']:
                        target = mount / row['path']
                        if os.path.lexists(target):
                            raise RuntimeError('Retain prior runtime file: ' + row['path'])
                        for parent in target.parents:
                            if parent == mount:
                                break
                            if parent.is_symlink():
                                raise RuntimeError('Unexpected runtime target parent symlink')
                    backup = mount / 'pong/stock-config/launchd.pre-native-board-runtime.plist'
                    if backup.exists():
                        raise RuntimeError('Retain previous board operation')
                    shutil.copy2(cache_path, backup)
                    for row in runtime['files']:
                        target = mount / row['path']
                        target.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy2(args.runtime / 'overlay' / row['path'], target)
                        os.chown(target, 0, 0)
                        target.chmod(row['mode'])
                    cache_path.write_bytes(cache_bytes)
                    os.chown(cache_path, 0, 0)
                    cache_path.chmod(0o644)
                for row in runtime['files']:
                    path = mount / row['path']
                    metadata = path.lstat()
                    if path.is_symlink() or sha(path) != row['sha256'] or metadata.st_uid or metadata.st_gid or metadata.st_mode & 0o777 != row['mode']:
                        raise RuntimeError('Actual board runtime hash/metadata differs')
                subprocess.run(['codesign', '--verify', '--strict', str(mount / 'pong/wifi-bootstrap')], check=True)
            finally:
                subprocess.run(['hdiutil', 'detach', disk], check=True)
        report.update(status='LOCAL_STAGED_READBACK_VERIFIED', readonly_readback=True, files=runtime['files'],
                      launchd_cache_sha256=cache_sha, pending=['stock-kext closure', 'real UUID/isolated Data installation', 'hardware qualification'])
    except BaseException as error:
        report.update(status='FAIL', reason=str(error))
        raise
    finally:
        report['finished_at'] = datetime.now().astimezone().isoformat()
        record.write_text(json.dumps(report, indent=2) + '\n')
    print('Native board runtime locally staged/read back; no phone/Data write')


if __name__ == '__main__':
    main()
