#!/usr/bin/env python3
"""Build and stage the native Wi-Fi address service in a 22H374 OS copy."""
import argparse
import os
from pathlib import Path
import plistlib
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
LABEL = 'org.unixnu.zizhan.wifi-service'
DAEMON = 'System/Library/LaunchDaemons/' + LABEL + '.plist'
CACHE = 'System/Library/xpc/launchd.plist'
PROGRAM = 'pong/wifi-service18'
NAMER_INFO = 'System/Library/SystemConfiguration/InterfaceNamer.bundle/Info.plist'


def stage_interface_namer(root, original_root=None):
    destination = root / NAMER_INFO
    source = destination if destination.is_file() else original_root / NAMER_INFO if original_root else None
    if source is None or not source.is_file():
        raise RuntimeError('stock InterfaceNamer bundle metadata required')
    info = plistlib.loads(source.read_bytes())
    if info.get('CFBundleIdentifier') != 'com.apple.SystemConfiguration.InterfaceNamer':
        raise RuntimeError('unexpected InterfaceNamer bundle identity')
    # Stock configd waits 300/240 seconds for the stack/IOKit. This port
    # publishes its real named en0 after station readiness; other unsupported
    # devices can keep the global registry busy. Use configd's own bounded
    # fallback, which reports TIMEOUT rather than fabricating IOKit Quiet.
    info['WaitStackTimeout'] = 30.0
    info['WaitQuietTimeout'] = 10.0
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_BINARY))
    destination.chmod(0o644)


def compile_service(output, developer):
    env = dict(os.environ, DEVELOPER_DIR=str(developer))
    sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], env=env, text=True).strip()
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['xcrun', '--sdk', 'iphoneos', 'clang', '-target', 'arm64-apple-ios18.0',
        '-isysroot', sdk, '-Os', '-fobjc-arc', '-fblocks', '-Wall', '-Wextra', '-Werror',
        '-framework', 'Foundation', '-Wl,-no_fixup_chains', str(HERE / 'wifi-service18.m'),
        '-o', str(output)], env=env, check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', '--pagesize', '4096', str(output)], check=True)
    subprocess.run(['codesign', '--verify', '--strict', str(output)], check=True)


def stage_service(root, binary, original_root=None):
    root = root.resolve()
    cache_path = root / CACHE
    cache = plistlib.loads(cache_path.read_bytes())
    if cache.get('VersionNumber') != 7 or not isinstance(cache.get('LaunchDaemons'), dict):
        raise RuntimeError('expected the prepared 22H374 launchd cache')
    version = root / 'System/Library/CoreServices/SystemVersion.plist'
    if version.is_file() and plistlib.loads(version.read_bytes()).get('ProductBuildVersion') != '22H374':
        raise RuntimeError('refusing to stage on a different OS build')
    stage_interface_namer(root, original_root)
    job = {'Label': LABEL, 'ProgramArguments': ['/' + PROGRAM], 'RunAtLoad': True,
           'UserName': 'root', 'GroupName': 'wheel', 'StandardErrorPath': '/dev/console',
           'ExitTimeOut': 3}
    job_path = root / DAEMON
    if job_path.exists() and plistlib.loads(job_path.read_bytes()).get('Label') != LABEL:
        raise RuntimeError('Wi-Fi job path is already owned by a different service')
    dest = root / PROGRAM
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(binary, dest)
    dest.chmod(0o755)
    job_path.parent.mkdir(parents=True, exist_ok=True)
    job_path.write_bytes(plistlib.dumps(job, fmt=plistlib.FMT_BINARY))
    job_path.chmod(0o644)
    cache['LaunchDaemons']['/' + DAEMON] = job
    # Preserve the stock server and all its XPC endpoints. This is the
    # missing 22H374 MobileWiFi receive right, not a replacement server.
    native = [(key, value) for key, value in cache['LaunchDaemons'].items()
              if value.get('Label') == 'com.apple.wifid']
    if len(native) != 1:
        raise RuntimeError('expected exactly one stock wifid job')
    native_path, native_job = native[0]
    ports = native_job.setdefault('MachServices', {})
    manager = ports.get('com.apple.wifi.manager')
    ports['com.apple.wifi.manager'] = dict(manager, HideUntilCheckIn=True) if isinstance(manager, dict) else {'HideUntilCheckIn': True}
    native_file = root / native_path.lstrip('/')
    if not native_file.is_file():
        raise RuntimeError('stock wifid canonical plist missing')
    native_file.write_bytes(plistlib.dumps(native_job, fmt=plistlib.FMT_BINARY))
    cache_path.write_bytes(plistlib.dumps(cache, fmt=plistlib.FMT_BINARY))
    assert plistlib.loads(job_path.read_bytes()) == plistlib.loads(cache_path.read_bytes())['LaunchDaemons']['/' + DAEMON]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage', type=Path, required=True, help='mounted OS copy or prepared overlay')
    p.add_argument('--binary', type=Path, required=True, help='compiled, signed wifi-service18')
    p.add_argument('--original-root', type=Path, help='stock 22H374 root for metadata absent from an overlay')
    a = p.parse_args()
    stage_service(a.stage, a.binary, a.original_root)


if __name__ == '__main__':
    main()
