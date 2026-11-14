#!/usr/bin/env python3
"""Compile the maintained zizhan QCA6490 stack for native arm64e.

Keeps zizhan live-DT PHY/rail/clock inputs, actual Linux ath11k and shared
station shims. Only generated recipes change target/headers; no foreign
board profile, image write or radio activity is selected.
"""
import argparse
from datetime import datetime
import hashlib
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('native-build', 'linux', 'shared-root', 'firmware', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--external-firmware', action='store_true',
                        help='Use the reviewed native root PWF2 reader instead of embedding firmware in kernel TEXT')
    parser.add_argument('--native-source', type=Path,
                        help='Pinned native reference supplying the actual on-demand PWF2 reader')
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error('Use a new machine-local output directory')
    native = json.loads((args.native_build / 'host-core.json').read_text())
    kernel = args.native_build / 'artifacts/mach.development.pong.unstripped.noctf'
    if native.get('returncode') != 0 or not any(Path(row['path']).name == kernel.name and row['sha256'] == sha(kernel) for row in native['artifacts']):
        parser.error('Require the actual completed native source kernel/header tree')
    shared = args.shared_root.resolve()
    if subprocess.check_output(['git', '-C', str(shared / 'drivers/Ath12kWifi'), 'status', '--porcelain'], text=True).strip():
        parser.error('Shared Wi-Fi reference must be clean')
    if not (args.linux / 'drivers/net/wireless/ath/ath11k/pci.c').is_file():
        parser.error('Actual Linux ath11k input required')
    overlay = out / 'source-overlay'
    scripts = overlay / 'scripts'
    scripts.mkdir(parents=True)
    for name in ('pong_paths.py', 'build-pong-ath11k.py', 'build-pong-wifi-native.py', 'prepare-pong-wifi.py'):
        shutil.copy2(ROOT / 'scripts' / name, scripts / name)
    for name in ('PongWifi', 'NativeUfs', 'PongAdreno/ArmSmmu4K'):
        shutil.copytree(ROOT / 'drivers' / name, overlay / 'drivers' / name)
    external = None
    if args.external_firmware:
        pin = tomllib.loads((ROOT / 'configs/zizhan-ios18e-inputs.toml').read_text())['source']
        if not args.native_source or subprocess.check_output(['git', '-C', str(args.native_source), 'rev-parse', 'HEAD'], text=True).strip() != pin['commit']:
            parser.error('External PWF2 reader requires the exact pinned native source')
        reader = args.native_source / 'drivers/PongWifi/PongWifiFirmware.cpp'
        shutil.copy2(reader, overlay / 'drivers/PongWifi/PongWifiFirmware.cpp')
        path = overlay / 'drivers/PongWifi/PongWifiPlatform.cpp'
        text = path.read_text()
        declaration = 'extern const uint8_t pong_wifi_firmware[], pong_wifi_firmware_end[];'
        if text.count(declaration) != 1:
            raise RuntimeError('Review changed actual firmware boundary')
        text = text.replace(declaration, declaration + '\nbool pong_wifi_firmware_ready(void);\nint pong_wifi_firmware_get(const char *,const uint8_t **,size_t *);')
        first = text.index(' const size_t total=size_t(pong_wifi_firmware_end-pong_wifi_firmware);')
        last = text.index('\n}\nint cfgRead(', first)
        text = text[:first] + ' return pong_wifi_firmware_get(name,data,size);' + text[last:]
        guard = ' if(n==180){fail("UFS prerequisites absent; WiFi skipped");goto done;}'
        if text.count(guard) != 1:
            raise RuntimeError('Review changed real optional-worker prerequisite')
        text = text.replace(guard, guard + '\n for(n=0;n<180&&!pong_wifi_firmware_ready();n++)IOSleep(500);\n'
                            ' if(n==180){fail("native root zizhan firmware unavailable; WiFi skipped");goto done;}')
        path.write_text(text)
        external = {'reader_source_sha256': sha(reader), 'adapted_platform_sha256': sha(path),
                    'root_path': '/pong/firmware/qca6490.pwf',
                    'scope': 'root owner/mode/type/size and PWF2 bounds checked; bounded readiness before PCIe/radio; payloads loaded on demand'}
    (overlay / 'configs').mkdir()
    shutil.copy2(ROOT / 'configs/zizhan-wifi-sm8475.json', overlay / 'configs/zizhan-wifi-sm8475.json')
    tree = out / 'tree'
    (tree / '.cache').mkdir(parents=True)
    fakeroot = (args.native_build / 'tree/.cache/fakeroot').resolve()
    (tree / '.cache/fakeroot').symlink_to(fakeroot, target_is_directory=True)
    xnu = (args.native_build / 'tree/.cache/xnu-work').resolve()
    for name in ('build-pong-ath11k.py', 'build-pong-wifi-native.py'):
        path = scripts / name
        text = path.read_text().replace('arm64-apple-ios18.0', 'arm64e-apple-ios18.0')
        text = text.replace("'-arch', 'arm64'", "'-arch', 'arm64e'")
        text = text.replace("guest='22H374'", "guest='22G100', arch='arm64e'")
        if name == 'build-pong-wifi-native.py':
            marker = "'-DKERNEL=1'"
            text = text.replace(marker, "'-DXNU_PONG_NATIVE_PAC=1'," + marker)
            marker = "'-Wno-deprecated-declarations']"
            if text.count(marker) != 1:
                raise RuntimeError('Review changed maintained Wi-Fi compile flags')
            text = text.replace(marker, "'-I'+" + repr(str(xnu / 'EXTERNAL_HEADERS')) + ",'-I'+" + repr(str(xnu / 'libkern')) + ',' + marker)
            firmware_asm = "'arm64e-apple-ios18.0','-c',str(asm)"
            if text.count(firmware_asm) != 1:
                raise RuntimeError('Review changed firmware assembly target')
            text = text.replace(firmware_asm, "'arm64e-apple-ios18.0','-mkernel','-c',str(asm)")
            if args.external_firmware:
                selection = "for name in ('PongWifiPlatform','PongWifiNet'):"
                if text.count(selection) != 1:
                    raise RuntimeError('Review changed maintained Wi-Fi object selection')
                text = text.replace(selection, "for name in ('PongWifiPlatform','PongWifiNet','PongWifiFirmware'):")
                first = text.index(" obj=out/'PongWifiFirmware.o';subprocess.run(")
                last = text.index('\n', first)
                text = text[:first] + ' # Native root PWF2 reader selected; no embedded firmware assembly object.' + text[last:]
        path.write_text(text)
    env = dict(os.environ, PONG_SHENG_ROOT=str(shared), PONG_WORK=str(out),
               PONG_LLVM=str(Path('/opt/homebrew/opt/llvm@18').resolve()),
               PONG_LLVM_NM='/opt/homebrew/opt/llvm@18/bin/llvm-nm',
               PONG_LLVM_OBJCOPY='/opt/homebrew/opt/llvm@18/bin/llvm-objcopy')
    for key in ('PONG_WIFI_TEST_CONFIG', 'PONG_WIFI_DTB', 'PONG_HARDWARE_DTB'):
        env.pop(key, None)
    ath = out / 'ath11k'
    commands = [
        [sys.executable, str(scripts / 'build-pong-ath11k.py'), '--linux', str(args.linux.resolve()),
         '--shared', str(shared / 'drivers/Ath12kWifi'), '--output-dir', str(ath), '--jobs', '6'],
        [sys.executable, str(scripts / 'build-pong-wifi-native.py'), '--tree', str(tree),
         '--ath11k', str(ath / 'libPongAth11k.a'), '--firmware', str(args.firmware.resolve())]]
    report = {'started_at': datetime.now().astimezone().isoformat(), 'build': '22G100', 'arch': 'arm64e',
              'board': 'zizhan', 'device_access': False, 'runtime_qualified': False,
              'native_kernel_sha256': sha(kernel), 'commands': commands,
              'shared_commit': subprocess.check_output(['git', '-C', str(shared / 'drivers/Ath12kWifi'), 'rev-parse', 'HEAD'], text=True).strip(),
              'source_files': [{'path': str(path.relative_to(ROOT)), 'sha256': sha(path)}
                               for path in (ROOT / 'drivers/PongWifi').rglob('*') if path.is_file()],
              'generated_scripts': [{'name': path.name, 'sha256': sha(path)} for path in scripts.glob('*.py')],
              'profile_sha256': sha(ROOT / 'configs/zizhan-wifi-sm8475.json'),
              'external_firmware': external,
              'firmware': [{'name': path.name, 'bytes': path.stat().st_size, 'sha256': sha(path)}
                           for path in sorted(args.firmware.iterdir()) if path.is_file()]}
    record = out / 'receipt.json'
    record.write_text(json.dumps(report, indent=2) + '\n')
    report['stages'] = []
    for label, command in zip(('ath11k', 'board-network'), commands):
        with (out / (label + '.log')).open('w') as log:
            result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        report['stages'].append({'name': label, 'returncode': result.returncode})
        record.write_text(json.dumps(report, indent=2) + '\n')
        if result.returncode:
            raise SystemExit('Native Wi-Fi ' + label + ' compile failed; inspect retained log')
    report['artifacts'] = []
    report['object_abi'] = []
    for path in sorted(out.rglob('*.o')):
        header = struct.unpack('<4I', path.read_bytes()[:16])
        if header != (0xfeedfacf, 0x100000c, 0xc0000002, 1):
            raise RuntimeError('Native Wi-Fi object must retain kernel arm64e ABI: ' + str(path))
        report['object_abi'].append({'path': str(path.relative_to(out)), 'cpu_subtype': hex(header[2])})
    for path in (ath / 'libPongAth11k.a', tree / 'build/pong-wifi/libPongWifiNative.a'):
        report['artifacts'].append({'path': str(path), 'sha256': sha(path), 'bytes': path.stat().st_size})
    pack = tree / 'build/pong-wifi/PongWifiFirmware.bin'
    report['firmware_pack'] = {'path': str(pack), 'sha256': sha(pack), 'bytes': pack.stat().st_size,
                               'mode': 'external native root' if external else 'embedded kernel TEXT'}
    report.update(returncode=0, finished_at=datetime.now().astimezone().isoformat())
    record.write_text(json.dumps(report, indent=2) + '\n')
    print('HOST maintained zizhan native ath11k/board/network archive compiled; no radio/phone access')


if __name__ == '__main__':
    main()
