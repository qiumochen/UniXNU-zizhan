#!/usr/bin/env python3
"""Replay full-root and overlay staging without Apple files or a device."""
from pathlib import Path
import plistlib
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'userland'))
from wifi_service_stage import CACHE, DAEMON, NAMER_INFO, stage_service


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(plistlib.dumps(value, fmt=plistlib.FMT_BINARY))


with tempfile.TemporaryDirectory() as temporary:
    base = Path(temporary)
    binary = base / 'helper'
    binary.write_bytes(b'test fixture; not executable')
    stock = base / 'stock'
    info = {'CFBundleIdentifier': 'com.apple.SystemConfiguration.InterfaceNamer',
            'Builtin': True, 'Unrelated': 'preserved'}
    write(stock / NAMER_INFO, info)
    original = (stock / NAMER_INFO).read_bytes()
    for index, manager in enumerate((None, True, {'ResetAtClose': True})):
        root = base / str(index)
        native_path = '/System/Library/LaunchDaemons/com.apple.wifid.plist'
        ports = {'com.apple.wifi-xpc': True}
        if manager is not None:
            ports['com.apple.wifi.manager'] = manager
        job = {'Label': 'com.apple.wifid', 'MachServices': ports}
        write(root / native_path.lstrip('/'), job)
        write(root / CACHE, {'VersionNumber': 7, 'LaunchDaemons': {native_path: job}})
        write(root / 'System/Library/CoreServices/SystemVersion.plist', {'ProductBuildVersion': '22H374'})
        if index == 1:
            write(root / NAMER_INFO, info)
        stage_service(root, binary, stock)
        staged = plistlib.loads((root / NAMER_INFO).read_bytes())
        assert staged == dict(info, WaitStackTimeout=30.0, WaitQuietTimeout=10.0)
        cache = plistlib.loads((root / CACHE).read_bytes())
        native = plistlib.loads((root / native_path.lstrip('/')).read_bytes())
        assert native == cache['LaunchDaemons'][native_path]
        assert native['MachServices']['com.apple.wifi-xpc'] is True
        received = native['MachServices']['com.apple.wifi.manager']
        assert received['HideUntilCheckIn'] is True
        if isinstance(manager, dict):
            assert received['ResetAtClose'] is True
        assert plistlib.loads((root / DAEMON).read_bytes()) == cache['LaunchDaemons']['/' + DAEMON]
        assert (stock / NAMER_INFO).read_bytes() == original
        write(root / 'System/Library/CoreServices/SystemVersion.plist', {'ProductBuildVersion': 'other'})
        try:
            stage_service(root, binary, stock)
        except RuntimeError:
            pass
        else:
            raise AssertionError('different OS build accepted')
print('PASS: native InterfaceNamer root/overlay staging, wifid endpoint preservation and OS identity refusal')
