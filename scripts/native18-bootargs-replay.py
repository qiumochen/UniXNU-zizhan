#!/usr/bin/env python3
"""Review the actual native/stock boot-argument parser computational contract.

Discovers the stock internal parser from its complete FALSE/TRUE wrappers,
not from a graph name. Executes actual image instructions and an independent
host compile of the same public parser source on bounded inputs. Host PAC
abstraction does not qualify authentication. Only the internal five-argument
parser is anchored; platform PE_state and hardware boot-args remain separate.
"""
import argparse
import ctypes
from datetime import datetime
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'native-build', 'pairing', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain previous replay; choose a new output directory')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    from native18_replay import KernelReplay
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    pairing = json.loads(args.pairing.read_text())
    if pairing.get('build') != '22G100' or Path(pairing['original']).resolve() != args.stock_kernel.resolve() or Path(pairing['native']).resolve() != args.native_kernel.resolve():
        parser.error('Require actual-input fresh pairing')
    built = json.loads((args.native_build / 'host-core.json').read_text())
    if built.get('returncode') != 0 or not any(Path(row['path']).name == args.native_kernel.name and row['sha256'] == sha(args.native_kernel) for row in built['artifacts']):
        parser.error('Actual completed source build required')
    names = {}
    for address, rows in native.symbols().items():
        for name in rows:
            names.setdefault(name, set()).add(address)
    key = '_PE_parse_boot_argn_internal'
    if len(names.get(key, ())) != 1:
        parser.error('Native parser symbol must be unique')
    target = next(iter(names[key]))
    def bodies(image):
        starts = sorted(a for a in image.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in image.segments))
        return {first: image.read(first, last - first) for first, last in zip(starts, starts[1:]) if last - first < 8192}
    old, new = bodies(original), bodies(native)
    prefix = (0xd503245f, 0xaa0203e3, 0xaa0103e2, 0xaa0003e1, 0x90000008, 0x91000108, 0xf9405108, 0x9101b100)
    wrappers = []
    for address, code in old.items():
        if len(code) != 40:
            continue
        calls, refs, shape = scan(code, address)
        if tuple(shape[:8]) == prefix and shape[8] in (0x52800004, 0x52800024) and shape[9] == 0x14000000:
            if len(calls) != 1 or calls[0][0] != address + 36 or calls[0][2] or len(refs) != 1 or refs[0][0] != address + 20:
                raise RuntimeError('Complete stock boot-argument wrapper operand contract differs')
            wrappers.append((address, calls[0][1], refs[0][1], int(shape[8] == 0x52800024)))
    if len(wrappers) != 2 or {row[3] for row in wrappers} != {0, 1} or len({row[1:3] for row in wrappers}) != 1:
        raise RuntimeError('Require unique complete FALSE/TRUE wrappers to one actual parser/platform state')
    start = wrappers[0][1]
    if start not in old or not 512 <= len(old[start]) <= 4096 or not 512 <= len(new[target]) <= 4096:
        raise RuntimeError('Actual internal parser extent differs')
    existing = pairing['functions'].get(hex(start))
    if existing and int(existing['native'], 16) != target:
        raise RuntimeError('Direct parser computational contract contradicts current identity')
    args.out.mkdir(parents=True)
    source_path = args.native_build / 'tree/.cache/xnu-work/pexpert/gen/bootargs.c'
    source = source_path.read_text()
    # Compile the actual public C implementation independently of A64 code.
    # Remove only unrelated exported wrappers/XNUPOST tests and platform
    # headers; all parser/number/string/separator helper code is unchanged.
    first = source.index('typedef boolean_t (*argsep_func_t)')
    internal_end = source.index('\nboolean_t\nPE_parse_boot_argn(')
    helper_first = source.index('\nstatic boolean_t\nisargsep(')
    helper_end = source.index('\nboolean_t\nPE_imgsrc_mount_supported(')
    unit = args.out / 'actual-public-parser.c'
    unit.write_text('#include <stdint.h>\n#include <stddef.h>\n#include <string.h>\n'
                    '#undef __x86_64__\ntypedef int boolean_t;typedef int kern_return_t;\n'
                    '#define TRUE 1\n#define FALSE 0\n' + source[first:internal_end] + source[helper_first:helper_end] +
                    '\nint replay_public_parser(char*a,const char*k,void*out,int n,int f){return PE_parse_boot_argn_internal(a,k,out,n,f);}\n')
    library = args.out / 'actual-public-parser.dylib'
    subprocess.run(['xcrun', 'clang', '-std=c11', '-O2', '-fwrapv', '-dynamiclib', str(unit), '-o', str(library)], check=True)
    model = ctypes.CDLL(str(library.resolve())).replay_public_parser
    model.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    model.restype = ctypes.c_int
    left, right = KernelReplay(original), KernelReplay(native)
    def execute(machine, address, text, name, size, force):
        command, key_address, result = 0x50a0000, 0x50a1000, 0x50a2000
        machine.uc.mem_write(command, text.encode() + b'\0')
        machine.uc.mem_write(key_address, name.encode() + b'\0')
        machine.uc.mem_write(result, bytes([0xa5]) * 128)
        value = machine.run(address, [command, key_address, result, size & 0xffffffff, force])
        return value, bytes(machine.uc.mem_read(result, 128))
    vectors = []
    samples = [('', 'x'), ('-x -a b=3 y=42', '-a'), ('-x b=96 y=42', 'bx'),
               ('foo=42', 'foo'), ('foo=-1', 'foo'), ('foo=0x1234', 'foo'),
               ('foo=077', 'foo'), ('foo=0b1011', 'foo'), ('foo=2k', 'foo'),
               ('foo=3M', 'foo'), ('foo=1g', 'foo'), ('foo=hello', 'foo'),
               ('_text=abcdefghijklmnopqrstuvw', '_text'), ('flag', 'flag'),
               ('a=1\tfoo=31 b=2', 'foo'), ('foo=1 foo=2', 'foo'),
               ('foo =27', 'foo'), ('f=4 foo=5', 'foo'), ('foo=0xffffffffffffffff', 'foo')]
    for text, name in samples:
        for size in (-1, 0, 1, 2, 3, 4, 8, 16, 32):
            for force in (0, 1):
                vectors.append((text, name, size, force))
    rng = random.Random(0x22b007)
    for _ in range(700):
        number = rng.getrandbits(40)
        value = rng.choice((str(number), '-' + str(number), '0x' + format(number, 'x'), 'word' + str(number), '7k'))
        text = rng.choice(('a=1 ', '\t', '-v ', '')) + 'probe=' + value + rng.choice(('', ' other=2', '\t-x'))
        vectors.append((text, rng.choice(('probe', 'pro', 'missing', '-v')), rng.choice((-1, 0, 1, 2, 3, 4, 8, 16, 32)), rng.randrange(2)))
    for text, name, size, force in vectors:
        before = execute(left, start, text, name, size, force)
        after = execute(right, target, text, name, size, force)
        output = ctypes.create_string_buffer(bytes([0xa5]) * 128, 128)
        value = model(text.encode(), name.encode(), output, size, force)
        expected = value, output.raw
        if before != after or after != expected:
            failure = {'text': text, 'key': name, 'size': size, 'force': force,
                       'stock': [before[0], before[1].hex()], 'native': [after[0], after[1].hex()],
                       'actual_public_c': [value, output.raw.hex()]}
            (args.out / 'disagreement.json').write_text(json.dumps(failure, indent=2) + '\n')
            raise RuntimeError('Actual stock/native/public-C parser computational contract disagrees')
    # Use a named API with direct source semantics; never publish emulated
    # instructions or alter its actual authentication implementation.
    report = {'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
              'stock_sha256': sha(args.stock_kernel), 'native_sha256': sha(args.native_kernel),
              'functions': [{'original': hex(start), 'name': key,
                             'evidence': 'Unique complete stock FALSE/TRUE boot-argument wrappers share this internal entry; actual stock/native code replays against independent host compile of actual public C parser over bounded numeric/string/boolean/missing/truncation cases; all output bytes and return codes agree; authentication abstracted only for host computation',
                             'original_sha256': hashlib.sha256(old[start]).hexdigest(), 'native_sha256': hashlib.sha256(new[target]).hexdigest(),
                             'public_source_sha256': sha(source_path), 'cases': len(vectors)}],
              'data': [], 'bootable': False,
              'replay': {'result': 'PASS', 'cases': len(vectors), 'vector_sha256': hashlib.sha256(json.dumps(vectors).encode()).hexdigest(),
                         'actual_public_c_extracted_sha256': sha(unit), 'source_instruction_sites': len(right.executed),
                         'authenticated_transfers_to_actual_code': len(right.transfers),
                         'limitations': 'bounded computational contract replay; PAC keys and platform bootargs initialization are not emulated/qualified'}}
    (args.out / 'anchors.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Actual stock/native/public-C parser computational replay PASS:', len(vectors), 'cases; one internal parser identity')


if __name__ == '__main__':
    main()
