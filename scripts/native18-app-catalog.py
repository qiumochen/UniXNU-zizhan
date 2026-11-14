#!/usr/bin/env python3
"""Prepare actual 22G100 application seeds and native LS registration overlay.

Retains stock removable-app migration. Does not embed removable apps as
builtins, retire placeholders, rebuild a Data database, mount a writable
volume or contact the phone. All outputs remain machine-local.
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
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('native-source', 'os-root', 'service-runtime', 'bootstrap-runtime', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--developer-dir', type=Path, default=Path('/Applications/Xcode.app/Contents/Developer'))
    args = parser.parse_args()
    if os.geteuid():
        parser.error('Actual native app seed metadata requires root ownership')
    source, os_root, out = args.native_source.resolve(), args.os_root.resolve(), args.out.resolve()
    pin = tomllib.loads((ROOT / 'configs/zizhan-ios18e-inputs.toml').read_text())['source']
    if subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() != pin['commit'] or subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True).strip():
        parser.error('Require clean exact native reference source')
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error('Use a new machine-local output directory')
    version_file = os_root / 'System/Library/CoreServices/SystemVersion.plist'
    version = plistlib.loads(version_file.read_bytes())
    info = plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', str(os_root)]))
    if (version.get('ProductVersion'), version.get('ProductBuildVersion')) != ('18.6.2', '22G100') or info.get('WritableVolume') is not False or not info.get('GlobalPermissionsEnabled'):
        parser.error('Require actual read-only, ownership-enabled native restore')
    services = json.loads((args.service_runtime / 'receipt.json').read_text())
    bootstrap = json.loads((args.bootstrap_runtime / 'receipt.json').read_text())
    for receipt in (services, bootstrap):
        if (receipt.get('build'), receipt.get('arch'), receipt.get('result')) != ('22G100', 'arm64e', 'HOST_VERIFIED'):
            parser.error('Require verified native service/bootstrap inputs')
    if not bootstrap.get('staged_app_seed'):
        parser.error('Require the actual scoped native app seed reader')
    binary = args.bootstrap_runtime / 'init'
    expected = {row['name']: row['sha256'] for row in bootstrap['files']}
    if sha(binary) != expected.get('init'):
        parser.error('Native bootstrap changed since its build')
    out.mkdir(parents=True)
    report = {'started_at': datetime.now().astimezone().isoformat(), 'source': pin, 'build': '22G100', 'arch': 'arm64e',
              'status': 'RUNNING', 'bootable': False, 'device_access': False, 'data_modified': False,
              'removable_policy': 'real restore seed and native SystemAppMigrator; no builtin conversion or migration success synthesis'}
    record = out / 'receipt.json'
    record.write_text(json.dumps(report, indent=2) + '\n')
    overlay = out / 'overlay'
    (overlay / 'System/Library/CoreServices').mkdir(parents=True)
    shutil.copy2(version_file, overlay / 'System/Library/CoreServices/SystemVersion.plist')
    sys.path[:0] = [str(source / 'scripts'), str(source / 'userland')]
    native = load('zizhan_native_catalog_support', source / 'userland/prepare-ios18e.py')
    destination = overlay / 'pong/native-staged-apps'
    shutil.copytree(os_root / 'private/var/staged_system_apps', destination, symlinks=True)
    for path in (destination, *destination.rglob('*')):
        os.chown(path, 0, 0, follow_symlinks=False)
    report['staged_apps'] = native.stage_native_staged_apps(overlay, os_root, out)
    catalog, identifiers = [], set()
    for folder, removable in ((os_root / 'Applications', False), (os_root / 'private/var/staged_system_apps', True)):
        for app in sorted(folder.glob('*.app')):
            path = app / 'Info.plist'
            if not path.is_file():
                continue
            metadata = plistlib.loads(path.read_bytes())
            identifier, executable = metadata.get('CFBundleIdentifier'), metadata.get('CFBundleExecutable')
            if not identifier or identifier in identifiers or not isinstance(executable, str) or '/' in executable or not (app / executable).is_file():
                raise RuntimeError('Require unique actual complete native app: ' + app.name)
            identifiers.add(identifier)
            row = {'Identifier': identifier}
            if not removable:
                row['Builtin'] = '/Applications/' + app.name
            catalog.append(row)
    if not 1 <= len(catalog) <= 256:
        raise RuntimeError('Native catalog exceeds runtime bounds')
    catalog_path = overlay / 'pong/native-system-apps.plist'
    catalog_path.write_bytes(plistlib.dumps({'Build': '22G100', 'EmbedRemovable': False, 'Applications': catalog}, fmt=plistlib.FMT_BINARY))
    runtime = out / 'native-adapters'
    adapters = load('zizhan_native_catalog_runtime', source / 'userland/native_services18.py')
    adapters.build_runtime(runtime, args.developer_dir)
    # Only the actual LS registration worker is selected from these host
    # adapter outputs. Other adapters need their own board/API qualification.
    shutil.copy2(runtime / 'system-app-register', overlay / 'pong/system-app-register')
    (overlay / 'sbin').mkdir()
    for relative in ('sbin/launchd', 'pong/init'):
        shutil.copy2(binary, overlay / relative)
    cache_source = args.service_runtime / 'overlay/System/Library/xpc/launchd.plist'
    cache = plistlib.loads(cache_source.read_bytes())
    relative = 'System/Library/LaunchDaemons/dev.unixnu.pong.system-app-register.plist'
    job = {'Label': 'dev.unixnu.pong.system-app-register', 'ProgramArguments': ['/pong/system-app-register'],
           'UserName': 'mobile', 'GroupName': 'mobile', 'RunAtLoad': True, 'ProcessType': 'Background',
           'EnvironmentVariables': {'HOME': '/var/mobile', 'CFFIXED_USER_HOME': '/var/mobile'},
           'StandardOutPath': '/dev/console', 'StandardErrorPath': '/dev/console'}
    cache['LaunchDaemons']['/' + relative] = job
    path = overlay / relative
    path.parent.mkdir(parents=True)
    path.write_bytes(plistlib.dumps(job, fmt=plistlib.FMT_BINARY))
    path = overlay / 'System/Library/xpc/launchd.plist'
    path.parent.mkdir(parents=True)
    path.write_bytes(plistlib.dumps(cache, fmt=plistlib.FMT_BINARY))
    # No migrator is removed or disabled for this catalog policy.
    migrator = 'System/Library/DataClassMigrators/SystemAppMigrator.migrator/Info.plist'
    migrator_info = plistlib.loads((os_root / migrator).read_bytes())
    if migrator_info.get('CFBundleIdentifier') != 'com.apple.MobileInstallation.SystemAppMigrator':
        raise RuntimeError('Actual native removable-app migrator identity differs')
    rows = []
    for path in sorted(overlay.rglob('*')):
        relative = str(path.relative_to(overlay))
        os.chown(path, 0, 0, follow_symlinks=False)
        if path.is_symlink():
            rows.append({'path': relative, 'symlink': os.readlink(path)})
        elif path.is_dir():
            if not relative.startswith('pong/native-staged-apps/'):
                path.chmod(0o755)
            rows.append({'path': relative, 'directory': True, 'mode': path.stat().st_mode & 0o777})
        else:
            if not relative.startswith('pong/native-staged-apps/'):
                path.chmod(0o755 if relative in ('sbin/launchd', 'pong/init', 'pong/system-app-register') else 0o644)
            rows.append({'path': relative, 'sha256': sha(path), 'mode': path.stat().st_mode & 0o777})
    for path in (overlay / 'sbin/launchd', overlay / 'pong/init', overlay / 'pong/system-app-register'):
        subprocess.run(['codesign', '--verify', '--strict', str(path)], check=True)
    (out / 'files.json').write_text(json.dumps(rows, indent=2) + '\n')
    report.update(status='HOST_PREPARED', catalog_entries=len(catalog), builtin_entries=sum('Builtin' in row for row in catalog),
                  files_manifest_sha256=sha(out / 'files.json'), previous_launchd_cache_sha256=sha(cache_source),
                  retained_migrator={'path': migrator, 'sha256': sha(os_root / migrator)},
                  bootstrap_sha256=sha(binary), source_bootstrap_receipt_sha256=sha(args.bootstrap_runtime / 'receipt.json'),
                  finished_at=datetime.now().astimezone().isoformat(),
                  unselected_adapters='other native adapter binaries compiled only; not copied into OS overlay')
    record.write_text(json.dumps(report, indent=2) + '\n')
    print('HOST native catalog prepared:', len(catalog), 'apps; native removable migration retained; no image/Data/phone write')


if __name__ == '__main__':
    main()
