#!/usr/bin/env python3
"""Identify typed object allocation from independently registered MetaClass callers.

API identity uses the caller's real class registration/PAC slot, exact complete
argument prefix, actual undefined stub name and full kalloc_type descriptor.
Later object constructors are outside this allocation API boundary and do not
need allocator implementation equivalence. No guest allocation is emulated.
"""
import argparse
from collections import defaultdict
from datetime import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import plistlib
import struct
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('prelink-kit', 'stock-kernel', 'native-kernel', 'kdk-extensions', 'class-reference', 'caller-research', 'out'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Preserve prior evidence; choose a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    from fileset_shape import checked_shape
    path = Path(__file__).with_name('native18-kext-import-names.py')
    spec = importlib.util.spec_from_file_location('typed_allocator_import_metadata', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    evidence = json.loads(args.class_reference.read_text())
    research = json.loads(args.caller_research.read_text())
    hashes = (sha(args.stock_kernel), sha(args.native_kernel))
    if (evidence['stock_sha256'], evidence['native_sha256']) != hashes or research['stock_sha256'] != hashes[0]:
        parser.error('Actual class/caller inputs differ')
    target_name = '_OSObject_typed_operator_new'
    source_targets = [address for address, names in native.symbols().items() if target_name in names]
    if len(source_targets) != 1:
        parser.error('Actual native typed allocator is not unique')
    target_source = source_targets[0]
    mapped = defaultdict(list)
    for row in evidence['caller_function_references']:
        if row['kind'] == 'registered-generated-metaclass-PAC-method' and any(
                name.endswith('9MetaClass5allocEv') for name in row['reference_names']):
            mapped[(row['component'], row['original'], row['reference'])].append(row)
    binaries = {row['component']: Path(row['binary']) for row in research['reports']}
    fixups = cache.fixups()
    loaded, proofs, controls = {}, [], []
    def descriptor(image, pointers, address, classname):
        section = image.section(address)
        if not section or section['name'] != '__kalloc_type' or (address - section['va']) % 64:
            raise ValueError('Allocator x0 does not identify a full typed descriptor')
        raw = image.read(address, 64)
        if any(raw[at:at + 8] != bytes(8) for at in (0, 8, 24, 48, 56)):
            raise ValueError('Typed descriptor initializer fields differ')
        name, signature = pointers.get(address + 16), pointers.get(address + 32)
        if not name or not signature or not isinstance(name[0], int) or not isinstance(signature[0], int):
            raise ValueError('Typed descriptor strings are not real local pointers')
        name_value, signature_value = image.cstring(name[0]), image.cstring(signature[0])
        if name_value != 'site.' + classname or signature_value is None:
            raise ValueError('Typed descriptor class-name/signature differs')
        flag, size = struct.unpack_from('<II', raw, 40)
        if flag & 0x60 != 0x60 or flag & 0x18 or not size:
            raise ValueError('Object allocator descriptor is data/VM/unversioned')
        return name_value, signature_value, flag, size, name[1:], signature[1:]
    # Exact 32-byte prologue and argument preparation. No earlier call, load
    # from arbitrary global, conditional, or parameter transform is omitted.
    prefix_shape = (0xd503237f, 0xa9be4ff4, 0xa9017bfd, 0x910043fd,
                    0x90000000, 0x91000000, 0x52800001, 0x94000000)
    def prefix(image, first, call_pc, expected_callee, pointers, classname):
        if call_pc - first != 28:
            raise ValueError('Allocation is not the exact first typed call')
        raw = image.read(first, 32)
        shape, _, calls, refs = checked_shape(raw, first)
        words = list(struct.unpack('<8I', raw))
        masked = list(shape)
        masked[6] &= 0xffe0001f
        if tuple(masked) != prefix_shape or calls != [(call_pc, expected_callee, True)] or len(refs) != 1:
            raise ValueError('Complete typed argument prefix differs')
        pc, address, kind = refs[0]
        if pc != first + 20 or kind != 'add':
            raise ValueError('Descriptor argument is not prepared at the exact x0 site')
        value = descriptor(image, pointers, address, classname)
        size = words[6] >> 5 & 0xffff
        if size != value[3]:
            raise ValueError('x1 allocation size differs from the complete class descriptor')
        return value, hashlib.sha256(raw).hexdigest()
    for row in research['calls']:
        if row['name'] != target_name:
            continue
        key = row['component'], row['caller'], row['reference_caller']
        witnesses = mapped.get(key, ())
        classes = {w['classname'] for w in witnesses}
        if len(classes) != 1:
            continue
        classname = next(iter(classes))
        owner, first, reference_first = row['component'], int(row['caller'], 16), int(row['reference_caller'], 16)
        if owner not in loaded:
            binary = binaries[owner]
            reference = Image(module.arm64e_slice(binary.read_bytes()))
            loaded[owner] = (Image(cache.data, cache.entries[owner][1]), reference,
                             module.indirect_imports(reference), module.chained_import_fixups(reference), sha(binary))
        stock, reference, imports, pointers, binary_hash = loaded[owner]
        if any(w['evidence']['reference_binary_sha256'] != binary_hash for w in witnesses):
            raise RuntimeError('Typed caller proof reference binary changed')
        # Each caller's reference stub is checked directly against its actual
        # undefined symbol table, rather than trusting the research name.
        stub, target = int(row['reference_stub'], 16), int(row['original'], 16)
        if imports['stubs'].get(stub) != target_name or not any(lo <= target < hi for lo, hi in original.mapped()):
            raise RuntimeError('Actual typed allocation import target is invalid')
        try:
            left, left_hash = prefix(stock, first, first + 28, target, fixups, classname)
            right, right_hash = prefix(reference, reference_first, reference_first + 28, stub, pointers, classname)
        except ValueError:
            continue
        if left != right:
            continue
        proofs.append({'component': owner, 'classname': classname, 'original': hex(target),
                       'stock_caller': hex(first), 'reference_caller': hex(reference_first),
                       'reference_stub': hex(stub), 'descriptor': left, 'stock_prefix_sha256': left_hash,
                       'reference_prefix_sha256': right_hash, 'typed_caller': witnesses})
        if not controls:
            # Mutate actual operands and require the exact prefix/descriptor
            # contract to reject; the real allocator is never executed.
            for label, delta in (('wrong-argument-size', 1 << 5), ('wrong-descriptor-address', 1 << 10)):
                changed = bytearray(stock.data)
                site = stock.offset(first + (24 if label == 'wrong-argument-size' else 20), 4)
                word, = struct.unpack_from('<I', changed, site)
                struct.pack_into('<I', changed, site, word ^ delta)
                altered = Image(bytes(changed), stock.at)
                try:
                    prefix(altered, first, first + 28, target, fixups, classname)
                except (ValueError, KeyError):
                    controls.append({'name': label, 'result': 'PASS'})
                else:
                    raise RuntimeError('Wrong typed allocator argument accepted')
            try:
                prefix(reference, reference_first, reference_first + 28, stub + 16, pointers, classname)
            except ValueError:
                controls.append({'name': 'wrong-allocator-stub', 'result': 'PASS'})
            else:
                raise RuntimeError('Wrong imported allocator stub accepted')
    targets = {row['original'] for row in proofs}
    if len(targets) != 1 or len({row['component'] for row in proofs}) < 2 or len({row['classname'] for row in proofs}) < 2:
        raise RuntimeError('Need multiple independently typed class factories in multiple components')
    result = {'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
              'stock_sha256': hashes[0], 'native_sha256': hashes[1],
              'functions': [{'original': next(iter(targets)), 'name': target_name,
                             'evidence': 'Multiple independent literal/metaclass-registered PAC MetaClass::alloc callers pass the complete versioned same class signature/name/size typed descriptor in x0 and exact class size in x1 to their first call; exact32byte argument prefix including every relocation agrees; actual KDK undefined stub names the typed object allocator; later object construction is outside this API identity boundary',
                             'identity_proof_kind': 'registered-metaclass-typed-allocation-boundary-v1',
                             'source_definition': hex(target_source)}],
              'data': [], 'factory_proofs': proofs, 'negative_controls': controls,
              'device_access': False, 'runtime_allocator_qualified': False, 'bootable': False}
    args.out.mkdir(parents=True)
    (args.out / 'identity-anchors.json').write_text(json.dumps(result, indent=2) + '\n')
    (args.out / Path(__file__).name).write_bytes(Path(__file__).read_bytes())
    print('Typed allocation API:', len(proofs), 'registered class factories /',
          len({row['component'] for row in proofs}), 'components; negative controls', controls, flush=True)


if __name__ == '__main__':
    main()
