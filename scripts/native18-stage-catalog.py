#!/usr/bin/env python3
"""Apply a qualified native app catalog to the independent local OS image.

Checks prior bootstrap/Metal state, retains replaced host-candidate files,
preserves SystemAppMigrator, and independently reads the overlay back on a
read-only mount. Does not write a Data image or contact the phone.
"""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('candidate', 'restore-os', 'baseline-stage', 'metal-stage', 'catalog', 'parser-test', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--resume-failed-stage', type=Path,
                        help='Resume this exact retained ENOSPC operation after local-image expansion')
    args = parser.parse_args()
    if os.geteuid():
        parser.error('Local native ownership staging requires root')
    candidate, out = args.candidate.resolve(), args.out.resolve()
    image = candidate / 'OS.sparseimage'
    if candidate == ROOT or ROOT in candidate.parents or not image.is_file() or image.is_symlink() or image.samefile(args.restore_os):
        parser.error('Require independent machine-local OS sparseimage')
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error('Retain previous operations; select a new output')
    baseline = json.loads((args.baseline_stage / 'receipt.json').read_text())
    metal = json.loads((args.metal_stage / 'receipt.json').read_text())
    catalog = json.loads((args.catalog / 'receipt.json').read_text())
    tests = json.loads((args.parser_test / 'results.json').read_text())
    if baseline.get('status') != 'LOCAL_STAGED' or metal.get('status') != 'LOCAL_STAGED_READBACK_VERIFIED' or any(Path(row['candidate']).resolve() != image for row in (baseline, metal)):
        parser.error('Prior bootstrap/Metal staging receipts must identify this actual OS')
    if (catalog.get('build'), catalog.get('arch'), catalog.get('status')) != ('22G100', 'arm64e', 'HOST_PREPARED'):
        parser.error('Require the actual prepared native app catalog')
    if tests.get('result') != 'PASS' or not any(row.get('name') == 'actual-catalog' and row.get('pass') for row in tests.get('results', [])):
        parser.error('Require actual-catalog seed-parser replay')
    # Bind the replay to the actual same prepared files, not merely a run name.
    test_source = args.parser_test / 'actual-catalog/fixture/pong'
    if not test_source.is_symlink() or test_source.resolve() != (args.catalog / 'overlay/pong').resolve():
        parser.error('Actual parser replay belongs to a different catalog')
    manifest = args.catalog / 'files.json'
    if sha(manifest) != catalog['files_manifest_sha256']:
        parser.error('Prepared catalog manifest changed')
    failed = None
    if args.resume_failed_stage:
        failed = json.loads((args.resume_failed_stage / 'receipt.json').read_text())
        if failed.get('status') != 'FAIL' or '[Errno 28]' not in failed.get('reason', '') or Path(failed['candidate']).resolve() != image or failed.get('catalog_receipt_sha256') != sha(args.catalog / 'receipt.json') or failed.get('parser_results_sha256') != sha(args.parser_test / 'results.json'):
            parser.error('Resume requires the exact failed ENOSPC operation and unchanged inputs')
    rows = json.loads(manifest.read_text())
    overlay = args.catalog / 'overlay'
    for row in rows:
        relative = PurePosixPath(row['path'])
        if relative.is_absolute() or '..' in relative.parts:
            parser.error('Unsafe prepared overlay path')
        path = overlay / row['path']
        if 'symlink' in row:
            valid = path.is_symlink() and os.readlink(path) == row['symlink']
        elif row.get('directory'):
            valid = path.is_dir() and not path.is_symlink()
        else:
            valid = path.is_file() and not path.is_symlink() and sha(path) == row['sha256']
        if not valid:
            parser.error('Prepared source differs: ' + row['path'])
    out.mkdir(parents=True)
    mount = out / 'os-mount'
    mount.mkdir()
    report = {'started_at': datetime.now().astimezone().isoformat(), 'status': 'RUNNING', 'candidate': str(image),
              'build': '22G100', 'arch': 'arm64e', 'bootable': False, 'phone_written': False, 'data_image_modified': False,
              'catalog_receipt_sha256': sha(args.catalog / 'receipt.json'), 'parser_results_sha256': sha(args.parser_test / 'results.json')}
    if failed:
        report['resumed_failed_receipt_sha256'] = sha(args.resume_failed_stage / 'receipt.json')
        report['repaired_partial_files'] = []
    record = out / 'receipt.json'
    record.write_text(json.dumps(report, indent=2) + '\n')
    replacement = {'sbin/launchd', 'pong/init', 'System/Library/xpc/launchd.plist'}
    final_binaries = dict(baseline['binaries'])
    final_binaries.update({'sbin/launchd': catalog['bootstrap_sha256'], 'pong/init': catalog['bootstrap_sha256']})
    try:
        for readonly in (False, True):
            attached = plistlib.loads(subprocess.check_output(['hdiutil', 'attach', *(['-readonly'] if readonly else []),
                '-nobrowse', '-owners', 'on', '-mountpoint', str(mount), '-plist', str(image)]))
            disk = attached['system-entities'][0]['dev-entry']
            try:
                volume = plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', str(mount)]))
                if volume.get('WritableVolume') != (not readonly) or not volume.get('GlobalPermissionsEnabled') or volume['VolumeUUID'] != baseline['volume_uuid']:
                    raise RuntimeError('Candidate mount contract differs')
                version = plistlib.loads((mount / 'System/Library/CoreServices/SystemVersion.plist').read_bytes())
                if (version.get('ProductVersion'), version.get('ProductBuildVersion')) != ('18.6.2', '22G100'):
                    raise RuntimeError('Actual native OS version differs')
                expected = final_binaries if readonly else baseline['binaries']
                for relative, digest in expected.items():
                    allowed = {digest}
                    if failed and not readonly:
                        allowed.add(final_binaries[relative])
                    if sha(mount / relative) not in allowed:
                        raise RuntimeError('Bootstrap invariant differs: ' + relative)
                for relative, digest in metal['files'].items():
                    if sha(mount / relative) != digest:
                        raise RuntimeError('Previously verified Metal changed: ' + relative)
                if sha(mount / 'pong/etc/var.skel') != baseline['manifest_sha256']:
                    raise RuntimeError('Original native Data seed changed')
                migrator = catalog['retained_migrator']
                if sha(mount / migrator['path']) != migrator['sha256']:
                    raise RuntimeError('Original SystemAppMigrator changed')
                if not readonly:
                    required = sum((overlay / row['path']).stat().st_size for row in rows
                                   if 'sha256' in row) + (256 << 20)
                    if volume.get('APFSContainerFree', 0) < required:
                        raise RuntimeError('Local APFS capacity prerequisite failed before writes; expand the independent candidate')
                    cache_hashes = {catalog['previous_launchd_cache_sha256']}
                    if failed:
                        cache_hashes.add(sha(overlay / 'System/Library/xpc/launchd.plist'))
                    if sha(mount / 'System/Library/xpc/launchd.plist') not in cache_hashes:
                        raise RuntimeError('Prior launchd catalog changed')
                    backup = mount / 'pong/stock-config/pre-native-app-catalog'
                    if backup.exists() and not failed:
                        raise RuntimeError('Retain prior application operation')
                    if failed:
                        for relative in replacement:
                            digest = catalog['previous_launchd_cache_sha256'] if relative == 'System/Library/xpc/launchd.plist' else baseline['binaries'][relative]
                            if sha(backup / relative.replace('/', '_')) != digest:
                                raise RuntimeError('Original failed-operation rollback file differs')
                    # Preflight all collision and parent-link checks before writes.
                    for row in rows:
                        target = mount / row['path']
                        if target.is_symlink() and 'symlink' not in row:
                            raise RuntimeError('Unexpected target symlink: ' + row['path'])
                        for parent in target.parents:
                            if parent == mount:
                                break
                            if parent.is_symlink():
                                raise RuntimeError('Overlay target parent is a symlink')
                        if not row.get('directory') and os.path.lexists(target) and row['path'] not in replacement:
                            if failed and 'symlink' in row and target.is_symlink() and os.readlink(target) == row['symlink']:
                                continue
                            if failed and 'symlink' not in row and target.is_file() and not target.is_symlink():
                                digest = sha(target)
                                if digest == row.get('sha256'):
                                    continue
                                if row['path'].startswith('pong/native-staged-apps/'):
                                    report['repaired_partial_files'].append({'path': row['path'], 'previous_sha256': digest,
                                                                            'previous_bytes': target.stat().st_size,
                                                                            'intended_sha256': row['sha256']})
                                    continue
                            if row['path'] != 'System/Library/CoreServices/SystemVersion.plist' or sha(target) != row.get('sha256'):
                                raise RuntimeError('Retain unexpected existing target: ' + row['path'])
                    if not failed:
                        backup.mkdir()
                        for relative in replacement:
                            shutil.copy2(mount / relative, backup / relative.replace('/', '_'))
                    for row in rows:
                        source, target = overlay / row['path'], mount / row['path']
                        if row.get('directory'):
                            target.mkdir(exist_ok=True)
                        elif 'symlink' in row:
                            if not target.is_symlink():
                                target.symlink_to(row['symlink'])
                        else:
                            if not target.is_file() or sha(target) != row['sha256']:
                                shutil.copy2(source, target)
                        os.chown(target, 0, 0, follow_symlinks=False)
                        if 'symlink' not in row:
                            target.chmod(row['mode'])
                for row in rows:
                    path = mount / row['path']
                    metadata = path.lstat()
                    valid = metadata.st_uid == metadata.st_gid == 0
                    if 'symlink' in row:
                        valid &= path.is_symlink() and os.readlink(path) == row['symlink']
                    else:
                        valid &= not path.is_symlink() and metadata.st_mode & 0o777 == row['mode']
                        valid &= path.is_dir() if row.get('directory') else path.is_file() and sha(path) == row['sha256']
                    if not valid:
                        raise RuntimeError('Actual catalog hash/metadata differs: ' + row['path'])
                for relative in (*final_binaries, 'pong/system-app-register'):
                    subprocess.run(['codesign', '--verify', '--strict', str(mount / relative)], check=True)
            finally:
                subprocess.run(['hdiutil', 'detach', disk], check=True)
        report.update(status='LOCAL_STAGED_READBACK_VERIFIED', binaries=final_binaries,
                      catalog_entries=catalog['catalog_entries'], readonly_readback=True,
                      removable_migrator_retained=True, files_manifest_sha256=sha(manifest),
                      pending=['stock-kext closure', 'board network/power', 'real isolated UUID layout', 'native app installation/registration on device'])
    except BaseException as error:
        report.update(status='FAIL', reason=str(error))
        raise
    finally:
        report['finished_at'] = datetime.now().astimezone().isoformat()
        record.write_text(json.dumps(report, indent=2) + '\n')
    print('Native catalog/bootstrap locally staged and read back; SystemAppMigrator retained; no Data/phone write')


if __name__ == '__main__':
    main()
