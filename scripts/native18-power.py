#!/usr/bin/env python3
"""Compile maintained zizhan ADSP/battery/backlight for native arm64e.

Selects actual dual-pack battery and board display owners. The narrow power
registration omits the audio-start consumer; no amplifier/audio code or fake
profile is compiled. Firmware remains an external runtime prerequisite.
"""
import argparse
from datetime import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('native-source', 'native-build', 'class-abi', 'shared-root', 'out'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    out, source = args.out.resolve(), args.native_source.resolve()
    pin = tomllib.loads((ROOT / 'configs/zizhan-ios18e-inputs.toml').read_text())['source']
    if subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() != pin['commit']:
        parser.error('Wrong native source revision')
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error('Use a new machine-local output directory')
    built = json.loads((args.native_build / 'host-core.json').read_text())
    kernel = args.native_build / 'artifacts/mach.development.pong.unstripped.noctf'
    abi = json.loads((args.class_abi / 'receipt.json').read_text())
    if built.get('returncode') != 0 or sha(kernel) != abi['native_sha256'] or not any(row['sha256'] == sha(kernel) and Path(row['path']).name == kernel.name for row in built['artifacts']):
        parser.error('Require actual completed native kernel/header/ABI inputs')
    parent_path = args.class_abi / 'IOPMPowerSource.json'
    parent = json.loads(parent_path.read_text())
    if not parent.get('abi_compatible') or parent['classname'] != 'IOPMPowerSource':
        parser.error('Require actual native power-source PAC parent')
    out.mkdir(parents=True)
    overlay = out / 'source-overlay'
    shutil.copytree(ROOT / 'drivers/PongPower', overlay / 'drivers/PongPower')
    owner = overlay / 'drivers/PongPower/PongPower.cpp'
    text = owner.read_text()
    if text.count(' sheng_audio_probe();') != 1:
        raise RuntimeError('Review changed owned power registration boundary')
    owner.write_text(text.replace(' sheng_audio_probe();', ' // zizhan native battery stage: no audio-start consumer selected.'))
    framework = args.native_build / 'tree/.cache/fakeroot/System/Library/Frameworks/Kernel.framework/Versions/A'
    xnu = args.native_build / 'tree/.cache/xnu-work'
    env = dict(os.environ, DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
    sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], env=env, text=True).strip()
    flags = ['-target', 'arm64e-apple-ios18.0', '-isysroot', sdk, '-mkernel', '-ffreestanding', '-fno-builtin',
             '-fno-stack-protector', '-mgeneral-regs-only', '-fblocks', '-O2', '-DXNU_PONG_NATIVE_PAC=1',
             '-DKERNEL=1', '-DKERNEL_PRIVATE=1', '-DXNU_KERNEL_PRIVATE=1', '-DARM64_BOARD_CONFIG_PONG=1',
             '-DXNU_PONG_ADRENO_PLATFORM=1', '-I' + str(framework / 'PrivateHeaders'), '-I' + str(framework / 'Headers'),
             '-I' + str(xnu / 'EXTERNAL_HEADERS'), '-I' + str(xnu / 'libkern'),
             '-I' + str(overlay / 'drivers/PongPower'), '-I' + str(args.shared_root.resolve() / 'package'), '-Wno-deprecated-declarations']
    # Retain the original real late-initializer transformation; it does not
    # synthesize a metaclass or change its methods/PAC slots.
    sys.path.insert(0, str(ROOT / 'scripts'))
    late = module('zizhan_native_power_late', ROOT / 'scripts/build-pong-power-native.py')
    names = ('QcomSmem.c', 'QcomSmp2p.c', 'QcomGlink.c', 'QcomMdt.c', 'QcomQmp.c', 'ShengBattmgr.c',
             'PongFirmware.c', 'PongAdsp.cpp', 'PongBattery.cpp', 'PongPower.cpp', 'PongBacklight.cpp')
    objects, commands = [], []
    report = {'started_at': datetime.now().astimezone().isoformat(), 'source': pin['commit'],
              'build': '22G100', 'arch': 'arm64e', 'board': 'zizhan', 'device_access': False,
              'runtime_qualified': False, 'audio_selected': False, 'native_kernel_sha256': sha(kernel),
              'shared_root': str(args.shared_root.resolve()),
              'parent_sha256': sha(parent_path),
              'source_files': [{'path': str(path.relative_to(ROOT)), 'sha256': sha(path)}
                               for path in (ROOT / 'drivers/PongPower').rglob('*') if path.is_file()],
              'power_registration_sha256': sha(owner), 'commands': commands}
    record = out / 'receipt.json'
    record.write_text(json.dumps(report, indent=2) + '\n')
    artifacts = out / 'artifacts'
    artifacts.mkdir()
    for name in names:
        src = overlay / 'drivers/PongPower' / name
        obj = artifacts / (src.stem + '.o')
        cpp = src.suffix == '.cpp'
        cmd = ['xcrun', 'clang++' if cpp else 'clang', *flags,
               *(['-std=gnu++17', '-fno-exceptions', '-fno-rtti'] if cpp else []), '-c', str(src), '-o', str(obj)]
        commands.append(cmd)
        with (artifacts / (src.stem + '.log')).open('w') as log:
            result = subprocess.run(cmd, env=env, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            report.update(returncode=result.returncode, failed_source=name)
            record.write_text(json.dumps(report, indent=2) + '\n')
            raise SystemExit('Native maintained power compile failed: ' + name)
        if name in ('PongBattery.cpp', 'PongBacklight.cpp'):
            obj = late.late_init(obj, name, 'PongBatteryStaticInit' if name == 'PongBattery.cpp' else 'PongBacklightStaticInit')
        if struct.unpack('<4I', obj.read_bytes()[:16]) != (0xfeedfacf, 0x100000c, 0xc0000002, 1):
            raise RuntimeError('Power object differs from kernel arm64e ABI')
        objects.append(obj)
    bindings = module('zizhan_native_power_parent', source / 'scripts/pong_fileset_parent_bindings.py')
    report['owned_subclass'] = bindings.validate_owned(parent_path, artifacts / 'PongBattery.o', 'AppleSmartBattery')
    archive = artifacts / 'libZizhanNativePower18e.a'
    cmd = ['xcrun', 'libtool', '-static', '-o', str(archive), *map(str, objects)]
    subprocess.run(cmd, env=env, check=True)
    commands.append(cmd)
    report.update(returncode=0, finished_at=datetime.now().astimezone().isoformat(),
                  archive={'path': str(archive), 'bytes': archive.stat().st_size, 'sha256': sha(archive)})
    record.write_text(json.dumps(report, indent=2) + '\n')
    print('HOST maintained native ADSP/battery/backlight archive and power-source PAC ancestry compiled; no hardware test')


if __name__ == '__main__':
    main()
