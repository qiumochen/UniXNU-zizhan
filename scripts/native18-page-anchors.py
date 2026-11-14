#!/usr/bin/env python3
"""Derive page globals from unique concrete bootstrap stores on both kernels.

Does not require a guessed arm_init identity. The unique original MOV14 and
two complete address/store pairs must write CONST then writable data, APFS
must consume the first through actual fixups, and the source MOV12 pair must
target the actual named variables. Source compile flags assert the real4KiB
configuration. Emits data contracts only, never a fake function identity.
"""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'native-build', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain prior page-store proof; select a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, signed, scan
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    built = json.loads((args.native_build / 'host-core.json').read_text())
    if built.get('returncode') != 0 or not any(Path(row['path']).name == args.native_kernel.name and row['sha256'] == sha(args.native_kernel) for row in built['artifacts']):
        parser.error('Require actual completed native source build')
    names = {}
    for address, rows in native.symbols().items():
        for name in rows:
            names.setdefault(name, set()).add(address)
    for name in ('_PAGE_SHIFT_CONST', '_page_shift_user32', '_arm_init'):
        if len(names.get(name, ())) != 1:
            parser.error('Actual source page/bootstrap definition must be unique')
    def functions(image):
        starts = sorted(a for a in image.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in image.segments))
        return [(first, image.read(first, last - first)) for first, last in zip(starts, starts[1:]) if 20 <= last - first <= 65536]
    def address(page_word, store, pc):
        page = (pc & ~4095) + (signed((((page_word >> 5) & 0x7ffff) << 2) | ((page_word >> 29) & 3), 21) << 12)
        return page + ((store >> 10) & 0xfff) * 4
    found = []
    for first, code in functions(original):
        words = struct.unpack('<' + 'I' * (len(code) // 4), code)
        for index in range(len(words) - 4):
            if words[index] & 0xffffffe0 != 0x528001c0:
                continue
            value = words[index] & 31
            targets = []
            for distance in (1, 3):
                page, store = words[index + distance:index + distance + 2]
                if page & 0x9f000000 != 0x90000000 or store & 0xffc00000 != 0xb9000000 or store & 31 != value or (store >> 5) & 31 != page & 31:
                    break
                targets.append(address(page, store, first + (index + distance) * 4))
            if len(targets) != 2:
                continue
            sections = [original.section(target) for target in targets]
            if sections[0] and sections[1] and sections[0]['segment'] == '__DATA_CONST' and sections[1]['segment'] == '__DATA':
                found.append({'initializer': first, 'site': first + index * 4, 'targets': targets,
                              'code_sha256': hashlib.sha256(code[index * 4:(index + 5) * 4]).hexdigest()})
    if len(found) != 1:
        raise RuntimeError('Original paired page-store contract is not unique')
    old = found[0]
    source_function = next(iter(names['_arm_init']))
    source_code = next(code for first, code in functions(native) if first == source_function)
    calls, refs, _ = scan(source_code, source_function)
    expected = [next(iter(names[name])) for name in ('_PAGE_SHIFT_CONST', '_page_shift_user32')]
    source_stores = []
    for site, target, kind in refs:
        offset = site - source_function
        if target != expected[0] or kind != 'add' or offset + 20 > len(source_code):
            continue
        words = struct.unpack_from('<5I', source_code, offset)
        # add xB,#global; mov wN,#12; str wN,[xB]; adrp xC; str wN,[xC,#other]
        add, move, first_store, page, second_store = words
        base, value = add & 31, move & 31
        if move & 0xffffffe0 != 0x52800180 or first_store & 0xffc00000 != 0xb9000000 or first_store & 31 != value or (first_store >> 5) & 31 != base or (first_store >> 10) & 0xfff:
            continue
        if page & 0x9f000000 != 0x90000000 or second_store & 0xffc00000 != 0xb9000000 or second_store & 31 != value or (second_store >> 5) & 31 != page & 31:
            continue
        if address(page, second_store, site + 12) == expected[1]:
            source_stores.append({'site': site, 'code_sha256': hashlib.sha256(source_code[offset:offset + 20]).hexdigest()})
    if len(source_stores) != 1:
        raise RuntimeError('Actual source named page globals do not have one complete4KiB store contract')
    apfs = Image(cache.data, cache.entries['com.apple.filesystems.apfs'][1])
    consumers = [site for site, pointer in cache.fixups().items() if pointer[0] == old['targets'][0] and any(first <= site < last for first, last in apfs.mapped())]
    if not consumers:
        raise RuntimeError('Actual retained APFS no longer consumes the first page-shift slot')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    support = args.out.with_suffix('.source-abi')
    if support.exists():
        parser.error('Retain existing source page-geometry compile check')
    support.mkdir()
    unit = support / 'native-page-geometry.c'
    unit.write_text('#include <mach/vm_param.h>\n#include <arm64/proc_reg.h>\n'
                    '_Static_assert(ARM_PGSHIFT==12,"actual ARMv8.2 native page granule");\n'
                    '_Static_assert(sizeof(PAGE_SHIFT_CONST)==4,"runtime page shift storage width");\n'
                    '#if __ARM_16K_PG__\n#error native4KiB compile configuration required\n#endif\n')
    compile_record = args.native_build / 'tree/build/xnu/ios-size-obj/DEVELOPMENT_ARM64_PONG/osfmk/DEVELOPMENT/arm_init.o.json'
    metadata = json.loads(compile_record.read_text().strip().rstrip(','))
    command = metadata['arguments'].copy()
    command[command.index('-xc') + 1] = str(unit.resolve())
    command[command.index('-o') + 1] = str((support / 'native-page-geometry.o').resolve())
    for flag in ('-MJ', '-MF'):
        if flag in command:
            index = command.index(flag)
            del command[index:index + 2]
    with (support / 'compile.log').open('w') as log:
        subprocess.run(command, cwd=metadata['directory'], stdout=log, stderr=subprocess.STDOUT, check=True)
    rows = [{'original': hex(address), 'name': name,
             'evidence': 'Unique complete original MOV14/CONST+writable page-shift stores independently consumed by actual APFS, actual source named-variable MOV12/store pair and actual source compile page-granule assertions; no arm_init graph identity or adjacent BSS guess',
             'bootstrap_site': hex(old['site']), 'source_store_site': hex(source_stores[0]['site']),
             'original_store_sha256': old['code_sha256'], 'source_store_sha256': source_stores[0]['code_sha256'],
             'apfs_shift_slots': [hex(site) for site in consumers]}
            for address, name in zip(old['targets'], ('_PAGE_SHIFT_CONST', '_page_shift_user32'))]
    args.out.write_text(json.dumps({'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
                                   'stock_sha256': sha(args.stock_kernel), 'native_sha256': sha(args.native_kernel),
                                   'functions': [], 'data': rows, 'bootable': False,
                                   'page_geometry_direct_store_contract': {'qualified': True, 'stock_shift': 14, 'native_shift': 12,
                                                                            'compile_assertions_sha256': sha(unit), 'source_compile_record_sha256': sha(compile_record)}}, indent=2) + '\n')
    print('Actual page-global store/type/granule contracts qualified independently of bootstrap graph names')


if __name__ == '__main__':
    main()
