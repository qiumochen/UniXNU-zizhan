#!/usr/bin/env python3
"""Locate _MTLDevice in the actual 22G100 cache, then describe its metadata.

Reads cache metadata only. No guest code executes and no image is modified.
The class address is discovered in Metal's own class list, never copied from
another restore or a previous experiment.
"""
import argparse
import json
import mmap
from pathlib import Path
import plistlib
import struct
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('native-source', 'systemos-root', 'out'):
        parser.add_argument('--' + option, type=Path, required=True)
    args = parser.parse_args()
    profile = tomllib.loads((ROOT / 'configs/zizhan-ios18e-inputs.toml').read_text())
    if subprocess.check_output(['git', '-C', str(args.native_source), 'rev-parse', 'HEAD'], text=True).strip() != profile['source']['commit']:
        parser.error('Wrong pinned native source')
    version = plistlib.loads((args.systemos_root / 'System/Library/CoreServices/SystemVersion.plist').read_bytes())
    if (version['ProductVersion'], version['ProductBuildVersion']) != ('18.6.2', '22G100'):
        parser.error('Actual matching SystemOS is required')
    if args.out.exists():
        parser.error('Retain previous evidence; choose a new output')
    cache = args.systemos_root / 'System/Library/Caches/com.apple.dyld'
    files, maps, slides = [], [], []
    main_cache = None
    for path in sorted(cache.glob('dyld_shared_cache_arm64e*')):
        if path.suffix in ('.symbols', '.atlas'):
            continue
        handle = path.open('rb')
        data = mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ)
        files.append((handle, data))
        if path.name == 'dyld_shared_cache_arm64e':
            main_cache = data
        off, count = struct.unpack_from('<II', data, 16)
        for index in range(count):
            va, size, at = struct.unpack_from('<QQQ', data, off + index * 32)
            maps.append((va, va + size, at, data))
        off, count = struct.unpack_from('<II', data, 312)
        for index in range(count):
            va, size, _, at, length = struct.unpack_from('<QQQQQ', data, off + index * 56)
            if length:
                slide_version, = struct.unpack_from('<I', data, at)
                if slide_version != 5:
                    parser.error('This native reader requires actual slide-v5 metadata')
                base, = struct.unpack_from('<Q', data, at + 16)
                slides.append((va, va + size, base))

    def read(va, size):
        for start, end, offset, data in maps:
            if start <= va and va + size <= end:
                return data[offset + va - start:offset + va - start + size]
        raise ValueError('Unbacked cache metadata ' + hex(va))

    def pointer(va):
        raw, = struct.unpack('<Q', read(va, 8))
        for start, end, base in slides:
            if start <= va < end:
                if not raw:
                    return 0
                value = base + (raw & ((1 << 34) - 1))
                return value if raw >> 63 else value | (((raw >> 34) & 255) << 56)
        return raw

    off, count = struct.unpack_from('<II', main_cache, 448)
    metal = []
    for index in range(count):
        va, _, _, name, _ = struct.unpack_from('<QQQII', main_cache, off + index * 32)
        end = main_cache.find(b'\0', name)
        if main_cache[name:end] == b'/System/Library/Frameworks/Metal.framework/Metal':
            metal.append(va)
    if len(metal) != 1:
        parser.error('Metal cache image is not unique')
    image = metal[0]
    header = struct.unpack('<8I', read(image, 32))
    if header[0] != 0xfeedfacf or header[1] != 0x100000c or header[2] & 0xffffff != 2:
        parser.error('Metal image must be actual arm64e Mach-O')
    at, classes = image + 32, []
    for _ in range(header[4]):
        command, size = struct.unpack('<II', read(at, 8))
        if command == 0x19:
            sections, = struct.unpack('<I', read(at + 64, 4))
            for index in range(sections):
                section = read(at + 72 + index * 80, 80)
                if section[:16].rstrip(b'\0') != b'__objc_classlist':
                    continue
                address, length = struct.unpack_from('<QQ', section, 32)
                for slot in range(address, address + length, 8):
                    cls = pointer(slot)
                    ro = pointer(cls + 32) & 0x7ffffffffff8
                    name = pointer(ro + 24)
                    if read(name, 11) == b'_MTLDevice\0':
                        classes.append(cls)
        at += size
    if len(classes) != 1:
        parser.error('Actual _MTLDevice class is not unique')
    args.out.mkdir(parents=True)
    layout = args.out / 'layout.json'
    subprocess.run([sys.executable, str(args.native_source / 'scripts/describe-pong-metal-abi.py'),
                    '--build', '22G100', '--cache-directory', str(cache),
                    '--device-class', hex(classes[0]), '--output', str(layout)], check=True)
    result = json.loads(layout.read_text())
    if result['device']['instance_size'] != 600:
        parser.error('Native device extent differs; review the frontend ABI')
    (args.out / 'receipt.json').write_text(json.dumps({
        'build': '22G100', 'arch': 'arm64e', 'class_discovery': 'Metal __objc_classlist unique _MTLDevice',
        'metal_image': hex(image), 'device_class': hex(classes[0]),
        'cache_uuid': main_cache[88:104].hex(), 'runtime_observed': False,
        'device_access': False}, indent=2) + '\n')


if __name__ == '__main__':
    main()
