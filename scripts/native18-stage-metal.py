#!/usr/bin/env python3
"""Stage the matched native Metal bundle into the independent local OS copy.

Requires the prior bootstrap/seed staging receipt and a successful native
plugin build. Verifies unchanged bootstrap and independently reads the new
bundle back after a read-only reattach. No phone or Data image access.
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


def attach(image, mount, readonly):
    result = plistlib.loads(subprocess.check_output([
        'hdiutil', 'attach', *(['-readonly'] if readonly else []), '-nobrowse',
        '-owners', 'on', '-mountpoint', str(mount), '-plist', str(image)]))
    return result['system-entities'][0]['dev-entry']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('candidate', 'restore-os', 'baseline-stage', 'plugin-stage', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if os.geteuid():
        parser.error('Local APFS staging requires root ownership')
    candidate, out = args.candidate.resolve(), args.out.resolve()
    image = candidate / 'OS.sparseimage'
    if candidate == ROOT or ROOT in candidate.parents or image.is_symlink() or not image.is_file() or image.samefile(args.restore_os):
        parser.error('Require independent local sparseimage outside public sources')
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error('Use a new local operation receipt directory')
    baseline = json.loads((args.baseline_stage / 'receipt.json').read_text())
    if baseline.get('status') != 'LOCAL_STAGED' or Path(baseline['candidate']).resolve() != image or baseline.get('build') != '22G100':
        parser.error('Require the completed actual bootstrap staging receipt')
    built = json.loads((args.plugin_stage / 'receipt.json').read_text())
    if (built.get('stage'), built.get('build'), built.get('arch'), built.get('returncode')) != ('plugin', '22G100', 'arm64e', 0):
        parser.error('Require a successful actual native plugin build')
    runtime = args.plugin_stage / 'build'
    bundle = runtime / 'PongAdrenoMetal.bundle'
    binaries = ('PongAdrenoMetal', 'PongMetalBridge.dylib', 'libvulkan_freedreno.dylib')
    contents = {str(path.relative_to(bundle)) for path in bundle.rglob('*')}
    if contents != set(binaries) | {'Info.plist', '_CodeSignature', '_CodeSignature/CodeResources'}:
        parser.error('Review unexpected native plugin contents')
    if any(path.is_symlink() for path in bundle.rglob('*')):
        parser.error('Native plugin must contain ordinary owned files')
    subprocess.run(['codesign', '--verify', '--strict', str(bundle)], check=True)
    expected = {Path(row['path']).name: row['sha256'] for row in built['artifacts']}
    for name in binaries:
        path = bundle / name
        if path.is_symlink() or sha(path) != expected.get(name):
            parser.error('Plugin artifact differs from successful build')
        raw = path.read_bytes()[:12]
        if raw[:4] != b'\xcf\xfa\xed\xfe' or int.from_bytes(raw[8:12], 'little') & 0xffffff != 2:
            parser.error('All native plugin binaries must be arm64e')
        subprocess.run(['codesign', '--verify', '--strict', str(path)], check=True)
    info = plistlib.loads((bundle / 'Info.plist').read_bytes())
    if (info.get('CFBundleIdentifier'), info.get('NSPrincipalClass')) != ('dev.unixnu.pong.Metal', 'PongAdrenoMetalDevice'):
        parser.error('Unexpected native Metal discovery identity')
    if (runtime / 'PongAdrenoMetal.publish').read_text() != '1\n' or (runtime / 'PongAdrenoMetal.clients').read_text() != '*\n':
        parser.error('Review changed explicit discovery policy')
    out.mkdir(parents=True)
    report = {'started_at': datetime.now().astimezone().isoformat(), 'status': 'RUNNING',
              'candidate': str(image), 'build': '22G100', 'arch': 'arm64e',
              'baseline_receipt_sha256': sha(args.baseline_stage / 'receipt.json'),
              'plugin_receipt_sha256': sha(args.plugin_stage / 'receipt.json'),
              'phone_written': False, 'data_image_modified': False, 'bootable': False}
    record = out / 'receipt.json'
    record.write_text(json.dumps(report, indent=2) + '\n')
    mount = out / 'os-mount'
    mount.mkdir()
    files = {}
    try:
        for readonly in (False, True):
            disk = attach(image, mount, readonly)
            try:
                volume = plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', str(mount)]))
                if not volume.get('GlobalPermissionsEnabled') or volume['WritableVolume'] != (not readonly) or volume['VolumeUUID'] != baseline['volume_uuid']:
                    raise RuntimeError('Actual candidate mount ownership/access/UUID differs')
                version = plistlib.loads((mount / 'System/Library/CoreServices/SystemVersion.plist').read_bytes())
                if (version.get('ProductVersion'), version.get('ProductBuildVersion')) != ('18.6.2', '22G100'):
                    raise RuntimeError('Actual OS is not the selected restore')
                for relative, digest in baseline['binaries'].items():
                    if sha(mount / relative) != digest:
                        raise RuntimeError('Previously verified bootstrap changed: ' + relative)
                if sha(mount / 'pong/etc/var.skel') != baseline['manifest_sha256']:
                    raise RuntimeError('Previously verified Data seed changed')
                extensions = mount / 'System/Library/Extensions'
                if extensions.is_symlink():
                    raise RuntimeError('Extensions must be a real owned directory')
                target = extensions / bundle.name
                if not readonly:
                    if target.exists() or target.is_symlink():
                        raise RuntimeError('Retain existing bundle; this operation only stages once')
                    extensions.mkdir(parents=True, exist_ok=True)
                    shutil.copytree(bundle, target)
                    for path in (target, *target.rglob('*')):
                        if path.is_symlink():
                            raise RuntimeError('Native bundle must contain ordinary files')
                        os.chown(path, 0, 0)
                        path.chmod(0o755 if path.is_dir() or path.name in binaries else 0o644)
                    for name in ('PongAdrenoMetal.publish', 'PongAdrenoMetal.clients'):
                        path = extensions / name
                        if path.exists() or path.is_symlink():
                            raise RuntimeError('Retain prior discovery policy')
                        shutil.copyfile(runtime / name, path)
                        os.chown(path, 0, 0)
                        path.chmod(0o644)
                    for path in target.rglob('*'):
                        if path.is_file():
                            relative = str(path.relative_to(mount))
                            files[relative] = sha(path)
                    for name in ('PongAdrenoMetal.publish', 'PongAdrenoMetal.clients'):
                        files[str((extensions / name).relative_to(mount))] = sha(extensions / name)
                for relative, digest in files.items():
                    path = mount / relative
                    metadata = path.stat()
                    mode = 0o755 if path.name in binaries else 0o644
                    if path.is_symlink() or sha(path) != digest or metadata.st_uid or metadata.st_gid or metadata.st_mode & 0o777 != mode:
                        raise RuntimeError('Actual staged file hash/owner/mode differs: ' + relative)
                for name in binaries:
                    subprocess.run(['codesign', '--verify', '--strict', str(target / name)], check=True)
                subprocess.run(['codesign', '--verify', '--strict', str(target)], check=True)
            finally:
                subprocess.run(['hdiutil', 'detach', disk], check=True)
        report.update(status='LOCAL_STAGED_READBACK_VERIFIED', files=files,
                      bootstrap_unchanged=True, readonly_readback=True,
                      pending=['stock-kext closure', 'full native catalog/board/network', 'isolated device layout', 'hardware qualification'])
    except BaseException as error:
        report.update(status='FAIL', reason=str(error))
        raise
    finally:
        report['finished_at'] = datetime.now().astimezone().isoformat()
        record.write_text(json.dumps(report, indent=2) + '\n')
    print('Native Metal staged/read back on local OS only; no phone/Data write')


if __name__ == '__main__':
    main()
