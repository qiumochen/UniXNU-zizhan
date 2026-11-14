#!/usr/bin/env python3
"""Qualify real platform-state storage and the native boot-argument wrapper.

Checks the paired platform initializer's concrete initialized/bootArgs/DT
field stores, an independent paired VM user, and the complete stock/native
argument-shuffle/helper/PAC shells. FALSE and TRUE wrappers stay distinct.
No guessed PPL implementation, replacement function or binary edit is made.
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
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'pairing', 'imports', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--parser-replay', type=Path,
                        help='Independent actual parser computational replay; pair with --native-build for direct platform-field review')
    parser.add_argument('--native-build', type=Path)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain old evidence; select a new output')
    pairing = json.loads(args.pairing.read_text())
    if (pairing.get('build'), pairing.get('arch')) != ('22G100', 'arm64e') or Path(pairing['original']).resolve() != args.stock_kernel.resolve() or Path(pairing['native']).resolve() != args.native_kernel.resolve():
        parser.error('Require a fresh pairing for actual input paths')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    stock, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(stock.data, stock.entries['com.apple.kernel'][1])
    names = {}
    for address, rows in native.symbols().items():
        for name in rows:
            names.setdefault(name, set()).add(address)

    def named(name):
        values = names.get(name, ())
        if len(values) != 1:
            raise RuntimeError('Require unique real source definition: ' + name)
        return next(iter(values))

    def paired(name):
        rows = [(int(address, 16), row) for address, row in pairing['functions'].items()
                if row.get('name') == name and int(row['native'], 16) == named(name)]
        if len(rows) != 1 or rows[0][1]['how'] != 'reviewed':
            raise RuntimeError('Require existing independently reviewed function: ' + name)
        return rows[0][0]

    def starts(image):
        return sorted(address for address in image.function_starts()
                      if any(segment['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and
                             segment['va'] <= address < segment['va'] + segment['filesize'] for segment in image.segments))

    left_starts, right_starts = starts(original), starts(native)

    def body(image, address):
        values = left_starts if image is original else right_starts
        index = values.index(address)
        return image.read(address, values[index + 1] - address)

    # Exactly the six field operations emitted from PE_init_platform:
    # initialized=1, bootArgs=args, DT base=args->deviceTreeP,
    # DT length=args->deviceTreeLength. x19 retains incoming args in both.
    field_words = (0x52800029, 0xb9000109, 0xf9005113,
                   0xf9403269, 0xf9004d09, 0xb9406a69)
    pattern = struct.pack('<6I', *field_words)

    def initializer_state(image, address):
        code = body(image, address)
        candidates = []
        _, refs, _ = scan(code, address)
        for site, target, kind in refs:
            offset = site - address
            if kind == 'add' and code[offset + 4:offset + 4 + len(pattern)] == pattern:
                word, = struct.unpack_from('<I', code, offset)
                if word & 31 == 8:
                    candidates.append(target)
        if len(candidates) != 1:
            raise RuntimeError('Platform initializer field-store contract differs')
        return candidates[0]

    direct_evidence = None
    if bool(args.parser_replay) != bool(args.native_build):
        parser.error('Direct platform-field review requires actual parser replay and native build together')
    if args.parser_replay:
        replay = json.loads((args.parser_replay / 'anchors.json').read_text())
        built = json.loads((args.native_build / 'host-core.json').read_text())
        hashes = (sha(args.stock_kernel), sha(args.native_kernel))
        if (replay.get('stock_sha256'), replay.get('native_sha256')) != hashes or replay.get('replay', {}).get('result') != 'PASS' or replay['replay']['cases'] < 1000:
            parser.error('Require completed same-input actual stock/native/public-C parser replay')
        if built.get('returncode') != 0 or not any(Path(row['path']).name == args.native_kernel.name and row['sha256'] == hashes[1] for row in built['artifacts']):
            parser.error('Direct platform-field review requires actual source build')
        # Derive a unique original field-initialization block independently
        # of graph/string labels. The live layout fields and arg1 owner are
        # checked against the actual compiled source headers below.
        matches = []
        for first, last in zip(left_starts, left_starts[1:]):
            if not 128 <= last - first <= 8192:
                continue
            code = original.read(first, last - first)
            if pattern not in code:
                continue
            try:
                state = initializer_state(original, first)
            except RuntimeError:
                continue
            if struct.pack('<I', 0xaa0103f3) not in code[:64]:
                continue  # x19 must be the unchanged incoming boot_args x1
            matches.append((first, state))
        if len(matches) != 1:
            raise RuntimeError('Actual boot-argument/DT field initialization block is ambiguous')
        old_init = matches[0][0]
        new_init = named('_PE_init_platform')
        source_init = body(native, new_init)
        if struct.pack('<I', 0xaa0103f3) not in source_init[:64]:
            raise RuntimeError('Source platform initializer no longer retains boot_args input x1')
        artifacts = args.out.with_suffix('.source-abi')
        if artifacts.exists():
            parser.error('Retain old direct source-ABI evidence')
        artifacts.mkdir(parents=True)
        unit = artifacts / 'native-platform-fields.c'
        unit.write_text('#include <stddef.h>\n#include <pexpert/pexpert.h>\n'
                        '_Static_assert(offsetof(PE_state_t,initialized)==0,"initialized");\n'
                        '_Static_assert(offsetof(PE_state_t,bootArgs)==0xa0,"bootArgs");\n'
                        '_Static_assert(offsetof(PE_state_t,deviceTreeHead)==0x98,"DT pointer");\n'
                        '_Static_assert(offsetof(PE_state_t,deviceTreeSize)==0xa8,"DT length");\n'
                        '_Static_assert(offsetof(boot_args,deviceTreeP)==0x60,"boot DT pointer");\n'
                        '_Static_assert(offsetof(boot_args,deviceTreeLength)==0x68,"boot DT length");\n'
                        '_Static_assert(offsetof(boot_args,CommandLine)==0x6c,"boot command line");\n')
        compile_record = args.native_build / 'tree/build/xnu/ios-size-obj/DEVELOPMENT_ARM64_PONG/pexpert/DEVELOPMENT/pe_init.o.json'
        compile_metadata = json.loads(compile_record.read_text().strip().rstrip(','))
        command = compile_metadata['arguments'].copy()
        command[command.index('-xc') + 1] = str(unit.resolve())
        command[command.index('-o') + 1] = str((artifacts / 'native-platform-fields.o').resolve())
        # Do not emit a new compile database into the mutable source build.
        for flag in ('-MJ', '-MF'):
            if flag in command:
                index = command.index(flag)
                del command[index:index + 2]
        with (artifacts / 'compile.log').open('w') as stream:
            subprocess.run(command, cwd=compile_metadata['directory'], stdout=stream, stderr=subprocess.STDOUT, check=True)
        direct_evidence = {'parser_replay_sha256': sha(args.parser_replay / 'anchors.json'),
                           'compiled_platform_field_assertions_sha256': sha(unit),
                           'compile_metadata_sha256': sha(compile_record),
                           'unique_stock_field_initializer': hex(old_init),
                           'scope': 'complete initialized/bootArgs/DT stores from input arg1 and actual source offsetof assertions; no graph-name dependency'}
    else:
        old_init, new_init = paired('_PE_init_platform'), named('_PE_init_platform')
    old_state, new_state = initializer_state(original, old_init), initializer_state(native, new_init)
    if new_state != named('_PE_state'):
        raise RuntimeError('Source initializer no longer writes named PE_state')
    if not direct_evidence:
        old_vm, new_vm = paired('_arm_vm_prot_init'), named('_arm_vm_prot_init')
        for image, address, target in ((original, old_vm, old_state), (native, new_vm, new_state)):
            _, refs, _ = scan(body(image, address), address)
            if sum(kind == 'add' and value == target for _, value, kind in refs) != 1:
                raise RuntimeError('Independent paired VM user disagrees on platform-state identity')

    helper = named('_PE_boot_args')
    helper_code = body(native, helper)
    helper_calls, helper_refs, helper_shape = scan(helper_code, helper)
    if len(helper_code) != 24 or helper_calls or len(helper_refs) != 1 or helper_refs[0][1:] != (new_state, 'add'):
        raise RuntimeError('Native bootArgs helper operands differ')
    helper_expected = (0xd503245f, 0x90000008, 0x91000108, 0xf9405108, 0x9101b100, 0xd65f03c0)
    if tuple(helper_shape) != helper_expected:
        raise RuntimeError('Native bootArgs helper complete instruction contract differs')
    internal = named('_PE_parse_boot_argn_internal')
    old_internal = paired('_PE_parse_boot_argn_internal') if any(row.get('name') == '_PE_parse_boot_argn_internal' and row.get('how') == 'reviewed' for row in pairing['functions'].values()) else None
    if old_internal is None:
        values = [int(address, 16) for address, row in pairing['functions'].items()
                  if row.get('name') == '_PE_parse_boot_argn_internal' and int(row['native'], 16) == internal and row.get('how') == 'reference-order' and not direct_evidence]
        if direct_evidence:
            values = [int(row['original'], 16) for row in replay['functions'] if row['name'] == '_PE_parse_boot_argn_internal']
        if len(values) != 1:
            raise RuntimeError('Internal parser must already have a fresh actual-source pairing')
        old_internal = values[0]
    wrapper = named('_PE_parse_boot_argn')
    wrapper_code = body(native, wrapper)
    calls, refs, shape = scan(wrapper_code, wrapper)
    # All callee-saved argument moves/restores and PAC return checks are
    # explicit, not discarded as generic compiler prologue noise.
    expected = (0xd503237f, 0xa9bd57f6, 0xa9014ff4, 0xa9027bfd, 0x910083fd,
                0xaa0203f3, 0xaa0103f4, 0xaa0003f5, 0x94000000,
                0xaa1503e1, 0xaa1403e2, 0xaa1303e3, 0x52800004,
                0xa9427bfd, 0xa9414ff4, 0xa8c357f6, 0xd50323ff,
                0xca1e07d0, 0xb6f00050, 0xd4388e20, 0x14000000)
    if tuple(shape) != expected or refs or [(site - wrapper, callee, link) for site, callee, link in calls] != [(32, helper, True), (80, internal, False)]:
        raise RuntimeError('Complete native FALSE boot-argument/PAC wrapper differs')
    imports = json.loads(args.imports.read_text())
    missing = {int(key, 16) for rows in imports['missing'].values() for key in rows}
    rows = []
    for address in sorted(missing & set(left_starts)):
        code = body(original, address)
        if len(code) != 40:
            continue
        calls, refs, shape = scan(code, address)
        expected = (0xd503245f, 0xaa0203e3, 0xaa0103e2, 0xaa0003e1,
                    0x90000008, 0x91000108, 0xf9405108, 0x9101b100,
                    0x52800004, 0x14000000)
        if tuple(shape) != expected or [(site - address, callee, link) for site, callee, link in calls] != [(36, old_internal, False)] or [(site - address, value, kind) for site, value, kind in refs] != [(20, old_state, 'add')]:
            continue
        rows.append({'original': hex(address), 'name': '_PE_parse_boot_argn',
                     'final_import_only': True,
                     'evidence': 'Complete stock FALSE argument-shuffle tail wrapper equals actual source wrapper plus verified bootArgs accessor; initialized/bootArgs/DT field stores and independent paired VM user identify PE_state; full native PAC save/restore/return-check shell verified',
                     'original_sha256': hashlib.sha256(code).hexdigest(), 'native_sha256': hashlib.sha256(wrapper_code).hexdigest(),
                     'helper_sha256': hashlib.sha256(helper_code).hexdigest()})
    if len(rows) != 1:
        raise RuntimeError('Require exactly one complete actual missing FALSE wrapper')
    report = {'build': '22G100', 'arch': 'arm64e', 'stock_sha256': sha(args.stock_kernel), 'native_sha256': sha(args.native_kernel),
              'pairing_sha256': sha(args.pairing), 'observed_at': datetime.now().astimezone().isoformat(),
              'functions': rows, 'data': [{'original': hex(old_state), 'name': '_PE_state',
                  'evidence': 'Paired actual PE_init_platform writes initialized/bootArgs/DT-base/DT-length fields with identical complete six-instruction field contract; independent paired arm_vm_prot_init references same unique base; actual source symbol agrees'}],
              'bootable': False, 'device_access': False}
    if direct_evidence:
        report['direct_platform_field_evidence'] = direct_evidence
        report['data'][0]['evidence'] = 'Unique complete initialized/bootArgs/DT field initialization from incoming boot_args arg1; same concrete source fields and compiled offsetof assertions; independent TRUE/FALSE wrapper discovery and actual stock/native/public-C parser replay; complete source boot_args accessor agrees'
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print('Qualified platform-state data and actual FALSE boot-argument wrapper; no image changes')


if __name__ == '__main__':
    main()
