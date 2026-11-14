#!/usr/bin/env python3
"""Recover caller-origin API naming proposals from real KDK import metadata.

Independent KDK kexts retain undefined symbols, indirect symbol tables and
authenticated import stubs. Compare complete caller instruction bodies with
the corresponding retained iOS component, then read the external name from
that exact callsite's stub. No stub order, nearby function order, or foreign
component code enters a binding. This first stage is research-only.
"""
import argparse
import copy
from collections import Counter, defaultdict
from datetime import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import plistlib
import struct
import sys


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def arm64e_slice(raw):
    if raw[:4] == b'\xcf\xfa\xed\xfe':
        cpu, subtype = struct.unpack_from('<II', raw, 4)
        if cpu != 0x100000c or subtype & 0xffffff != 2:
            raise ValueError('Require actual ARM64E input')
        return raw
    magic, count = struct.unpack_from('>II', raw)
    if magic != 0xcafebabe or count > 32:
        raise ValueError('Unsupported fat input')
    matches = []
    for index in range(count):
        cpu, subtype, offset, size, _ = struct.unpack_from('>IIIII', raw, 8 + index * 20)
        if cpu == 0x100000c and subtype & 0xffffff == 2 and offset + size <= len(raw):
            matches.append(raw[offset:offset + size])
    if len(matches) != 1:
        raise ValueError('Require exactly one ARM64E slice')
    return matches[0]


def indirect_imports(image):
    """Read actual S_SYMBOL_STUBS/S_NON_LAZY_SYMBOL_POINTERS metadata."""
    command = image.command(2)
    dynamic = image.command(0xb)
    if not command or not dynamic:
        raise ValueError('Require actual symbol and indirect-symbol commands')
    symoff, count, stroff, strsize = struct.unpack_from('<4I', image.data, command[0] + 8)
    indirectoff, indirectcount = struct.unpack_from('<II', image.data, dynamic[0] + 56)
    if symoff + 16 * count > len(image.data) or stroff + strsize > len(image.data) or \
            indirectoff + 4 * indirectcount > len(image.data):
        raise ValueError('Symbol metadata overruns input')
    symbols = []
    for index in range(count):
        name, kind, _, _, value = struct.unpack_from('<IBBHQ', image.data, symoff + 16 * index)
        if name >= strsize:
            raise ValueError('Bad symbol string index')
        end = image.data.index(b'\0', stroff + name, stroff + strsize)
        symbols.append((image.data[stroff + name:end].decode(), kind, value))
    result = {'stubs': {}, 'pointers': {}}
    for cmd, at, _ in image.commands:
        if cmd != 0x19:
            continue
        nsects, = struct.unpack_from('<I', image.data, at + 64)
        for index in range(nsects):
            offset = at + 72 + index * 80
            va, size = struct.unpack_from('<QQ', image.data, offset + 32)
            flags, first, stride = struct.unpack_from('<III', image.data, offset + 64)
            kind = flags & 0xff
            if kind not in (8, 6, 7):
                continue
            stride = stride if kind == 8 else 8
            if not stride or size % stride or first + size // stride > indirectcount:
                raise ValueError('Bad indirect section extent')
            for entry in range(size // stride):
                symindex, = struct.unpack_from('<I', image.data, indirectoff + 4 * (first + entry))
                if symindex & 0xc0000000:
                    continue
                if symindex >= len(symbols):
                    raise ValueError('Bad indirect symbol index')
                name, ntype, value = symbols[symindex]
                if ntype & 0xe0:
                    raise ValueError('Debug symbol in indirect import table')
                if ntype & 0x0e or value:
                    # Some kexts use indirect slots for local definitions.
                    # These are not external imports and provide no API name.
                    continue
                result['stubs' if kind == 8 else 'pointers'][va + entry * stride] = name
    return result


def chained_import_fixups(image):
    """Read arm64e kext rebase/bind chains without pretending binds are local."""
    command = image.command(0x80000034)
    if not command:
        return {}
    offset, length = struct.unpack_from('<II', image.data, command[0] + 8)
    if offset + length > len(image.data) or length < 28:
        raise ValueError('Truncated chained import metadata')
    version, starts, imports, strings, count, fmt, string_fmt = struct.unpack_from('<7I', image.data, offset)
    if version or fmt != 1 or string_fmt or max(starts, imports, strings) >= length or imports + 4 * count > length:
        raise ValueError('Unsupported actual kext chained import encoding')
    names = []
    for index in range(count):
        value, = struct.unpack_from('<I', image.data, offset + imports + 4 * index)
        start = offset + strings + (value >> 9)
        if not offset <= start < offset + length:
            raise ValueError('Chained import name overruns table')
        end = image.data.index(b'\0', start, offset + length)
        names.append(image.data[start:end].decode())
    table = offset + starts
    segments, = struct.unpack_from('<I', image.data, table)
    if segments > len(image.segments) or table + 4 + segments * 4 > offset + length:
        raise ValueError('Invalid chained segment index')
    result = {}
    for index in range(segments):
        relative, = struct.unpack_from('<I', image.data, table + 4 + index * 4)
        if not relative:
            continue
        at = table + relative
        size, page_size, ptr_format, segment_offset, _, pages = struct.unpack_from('<IHHQIH', image.data, at)
        if ptr_format != 7 or size < 22 + 2 * pages or at + size > offset + length or page_size not in (4096, 16384):
            raise ValueError('Unsupported actual ARM64E kext chain')
        for page, start in enumerate(struct.unpack_from('<' + 'H' * pages, image.data, at + 22)):
            if start == 0xffff:
                continue
            if start & 0x8000 or start >= page_size:
                raise ValueError('Unsupported multistart or out-of-page chain')
            site = image.base + segment_offset + page * page_size + start
            page_end = site - start + page_size
            while True:
                if site in result:
                    raise ValueError('Repeated chained fixup site')
                # ARM64E kernel chains have four-byte stride. A pointer may
                # start at page+4092 and straddle the next page; validate its
                # starting location here and all eight file bytes in read().
                if site >= page_end:
                    raise ValueError('Chained pointer starts outside its page')
                value, = struct.unpack('<Q', image.read(site, 8))
                auth, bind = value >> 63, value >> 62 & 1
                if bind:
                    ordinal = value & 0xffff
                    if ordinal >= len(names) or value >> 16 & 0xffff:
                        raise ValueError('Invalid import ordinal/reserved bits')
                    addend = value >> 32 & 0x7ffff if not auth else 0
                    if addend & 0x40000:
                        addend -= 0x80000
                    # Keep nonzero bind addends explicit and distinct from a
                    # callable symbol name. No identity producer accepts this
                    # tuple as an imported API or a local descriptor pointer.
                    target = (names[ordinal], addend) if addend else names[ordinal]
                else:
                    target = image.base + ((value & 0xffffffff) if auth else
                        (value & 0x7ffffffffff) | ((value >> 43 & 0xff) << 56))
                result[site] = (target, auth, value >> 49 & 3 if auth else None,
                                value >> 32 & 0xffff if auth else None, value >> 48 & 1 if auth else None)
                step = value >> 51 & 0x7ff
                if not step:
                    break
                site += step * 4
    return result


def caller_issues(a, b, first, target, imports, semantic, definition, hole,
                  implementation, stock, reference, typed_data=None, typed_functions=None,
                  imported_data=None, known_data=None, data_hole=None):
    """Validate the entire caller, leaving only one candidate API identity."""
    if a['shape'] != b['shape'] or not a['safe'] or not b['safe']:
        return ['unsafe-or-changed-complete-body']
    if len(a['calls']) != len(b['calls']) or len(a['refs']) != len(b['refs']):
        return ['operand-count']
    def position(row, pc, base):
        return row['positions'].get(pc - base)
    reasons = []
    for (pc, address, link), (npc, naddress, nlink) in zip(a['calls'], b['calls']):
        if position(a, pc, first) is None or position(a, pc, first) != position(b, npc, target) or link != nlink:
            reasons.append('call-position-kind')
            continue
        if first <= address < a['end']:
            if not target <= naddress < b['end'] or position(a, address, first) != position(b, naddress, target):
                reasons.append('local-branch')
        else:
            if address != hole and typed_functions and typed_functions.get(address) == naddress:
                continue
            name = imports['stubs'].get(naddress)
            resolved = definition(name) if name else None
            if address == hole:
                if resolved is None or resolved != implementation:
                    reasons.append('candidate-import-identity-differs')
                continue
            if resolved is None or semantic.get(address) != resolved:
                reasons.append('other-callee-not-independently-named')
    for (pc, address, kind, origin, width), (npc, naddress, nkind, norigin, nwidth) in zip(a['refs'], b['refs']):
        if position(a, pc, first) is None or position(a, pc, first) != position(b, npc, target) or (kind, width) != (nkind, nwidth) or \
                (origin is None) != (norigin is None) or origin is not None and position(a, origin, first) != position(b, norigin, target):
            reasons.append('literal-position-kind-producer')
            continue
        value = stock.cstring(address)
        if typed_data and typed_data.get(address) == naddress:
            continue
        item = imported_data.get((address, naddress)) if imported_data else None
        if item and (item['original'], item['native']) == data_hole:
            continue
        if item and known_data and known_data.get(item['original']) == item['native']:
            continue
        if value is None or value != reference.cstring(naddress):
            reasons.append('nonliteral-data-unproved')
    return reasons


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('native-source', 'prelink-kit', 'stock-kernel', 'kdk-extensions', 'qualified-map', 'out'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--all-stock-components', action='store_true',
                        help='Also inspect nonretained stock components as independent callers')
    parser.add_argument('--registered-data-reference', type=Path,
                        help='Actual independent class-registration data identities for caller operands')
    parser.add_argument('--typed-caller-prefix', action='store_true',
                        help='For independently typed callers, also prove exact entry-to-call operand boundaries')
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Preserve prior evidence; select a new private output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan, signed
    path = args.native_source / 'scripts/derive-pong-pac-shell-names.py'
    spec = importlib.util.spec_from_file_location('kext_actual_shell', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    cache = Image(args.stock_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    inside = lambda address: any(lo <= address < hi for lo, hi in original.mapped())
    qualified = json.loads(args.qualified_map.read_text())
    qualification_receipt = json.loads(args.qualified_map.with_name('receipt.json').read_text())
    if qualified.get('qualification') != 'INDEPENDENT_CONTRACTS_ONLY' or \
            qualification_receipt.get('stock_sha256') != sha(args.stock_kernel.read_bytes()):
        parser.error('Require the actual independent API-identity map for this stock input')
    native_path = Path(qualified['native'])
    if qualification_receipt.get('native_sha256') != sha(native_path.read_bytes()):
        parser.error('Independent API map native kernel changed')
    native = Image(native_path.read_bytes())
    native_names = defaultdict(set)
    for address, names in native.symbols().items():
        for name in names:
            native_names[name].add(address)
    def definition(name):
        values = native_names.get(name, ())
        return next(iter(values)) if len(values) == 1 else None
    semantic = {int(key, 16): int(row['native'], 16) for key, row in qualified['functions'].items()
                if row.get('semantic_identity') and (
                    row['how'] in ('qualified-direct-contract', 'qualified-complete-class-pac') or
                    row.get('evidence', '').startswith('Unique bidirectional complete instruction body'))}
    known_data = {int(key, 16): int(row['native'], 16) for key, row in qualified['data'].items()}
    stock_fixups = cache.fixups()
    typed_by_owner, typed_reference_hashes, typed_functions_by_owner = defaultdict(dict), defaultdict(set), defaultdict(dict)
    if args.registered_data_reference:
        evidence = json.loads(args.registered_data_reference.read_text())
        if (evidence['stock_sha256'], evidence['native_sha256']) != (sha(args.stock_kernel.read_bytes()), sha(native_path.read_bytes())):
            parser.error('Class-registration data reference belongs to different actual inputs')
        for row in evidence['caller_data_references']:
            if row['kind'] not in ('registered-metaclass', 'registered-instance-vtable', 'constructor-metaclass-vtable'):
                parser.error('Unknown typed class-data evidence')
            key, target = int(row['original'], 16), int(row['reference'], 16)
            old = typed_by_owner[row['component']].setdefault(key, target)
            typed_reference_hashes[row['component']].add(row['evidence']['reference_binary_sha256'])
            if old != target:
                parser.error('Class-data reference is ambiguous')
        candidates = defaultdict(lambda: defaultdict(set))
        for row in evidence.get('caller_function_references', []):
            if row['kind'] not in ('registered-class-unique-PAC-method', 'registered-generated-metaclass-PAC-method'):
                parser.error('Unknown typed local method evidence')
            candidates[row['component']][int(row['original'], 16)].add(int(row['reference'], 16))
        for owner, addresses in candidates.items():
            for address, targets in addresses.items():
                if len(targets) == 1:
                    typed_functions_by_owner[owner][address] = next(iter(targets))
    from fileset_emit import dependency_closure
    roots = ['com.apple.kec.corecrypto', 'com.apple.kec.pthread', 'com.apple.filesystems.apfs',
             'com.apple.kec.Compression', 'com.apple.AppleFSCompression.AppleFSCompressionTypeZlib',
             'com.apple.iokit.IOGPUFamily', 'com.apple.iokit.IOHIDFamily', 'com.apple.iokit.IOStorageFamily']
    retained = dependency_closure(cache, roots)
    known = {int(key, 16): row['name'] for key, row in qualified['functions'].items()
             if row.get('semantic_identity')}
    inventory = {}
    for folder in sorted(args.kdk_extensions.glob('*.kext')):
        info = folder / 'Contents/Info.plist'
        if not info.exists():
            continue
        meta = plistlib.loads(info.read_bytes())
        ident = meta.get('CFBundleIdentifier')
        executable = meta.get('CFBundleExecutable')
        if ident in cache.entries and executable and (args.all_stock_components or ident in retained):
            binary = folder / 'Contents/MacOS' / executable
            if binary.exists():
                inventory[ident] = binary
    rows, reports, certificates, negatives, data_certificates = [], [], [], {}, []
    def bodies(image):
        sections = [(row['va'], row['va'] + row['size']) for row in image.sections
                    if row['segment'] == '__TEXT_EXEC' and row['name'] in ('__text', '__hib_text')]
        starts = sorted(a for a in image.function_starts() if any(lo <= a < hi for lo, hi in sections))
        values, shapes = {}, defaultdict(list)
        for index, start in enumerate(starts):
            end = min([hi for lo, hi in sections if lo <= start < hi] +
                      ([starts[index + 1]] if index + 1 < len(starts) else []))
            if not 16 <= end - start <= 65536:
                continue
            value = module.shell(image, start, end)
            if value is None:
                continue
            shape, strings, positions = value
            shapes[shape].append(start)
            calls, refs, _ = scan(image.read(start, end - start), start, with_origins=True)
            raw = image.read(start, end - start)
            consumed = {row[3] for row in refs if row[3] is not None}
            safe = True
            for offset in range(0, len(raw), 4):
                word, = struct.unpack_from('<I', raw, offset)
                if word & 0x9f000000 == 0x90000000 and start + offset not in consumed or \
                        word & 0x3b000000 == 0x18000000:
                    safe = False
                bits = 19 if word & 0xff000010 == 0x54000000 or word & 0x7e000000 == 0x34000000 else \
                    14 if word & 0x7e000000 == 0x36000000 else 0
                if bits and not start <= start + offset + signed((word >> 5) & ((1 << bits) - 1), bits) * 4 < end:
                    safe = False
            values[start] = {'shape': shape, 'strings': strings, 'positions': positions,
                             'calls': calls, 'refs': refs, 'end': end, 'safe': safe}
        return values, shapes
    def prefix_body(image, body, start, stop):
        end = stop + 4
        position = body['positions'].get(stop - start)
        if position is None or position < 12 or not body['safe']:
            return None
        raw = image.read(start, end - start)
        for offset in range(0, len(raw), 4):
            word, = struct.unpack_from('<I', raw, offset)
            # A previous indirect transfer would introduce an unproved
            # receiver/callee contract; never ignore it in an entry slice.
            if word & 0xfe000000 == 0xd6000000:
                return None
            bits = 19 if word & 0xff000010 == 0x54000000 or word & 0x7e000000 == 0x34000000 else \
                14 if word & 0x7e000000 == 0x36000000 else 0
            if bits and not start <= start + offset + signed(word >> 5 & ((1 << bits) - 1), bits) * 4 < end:
                return None
        # Keep normalized code positions from the complete matched body.
        # No reachable forward local branch may leave the entry slice.
        for pc, target, link in body['calls']:
            if pc > stop:
                continue
            if not link and start <= target < body['end'] and target >= end:
                return None
        return dict(body, end=end, shape=body['shape'][:position // 4 + 1],
                    calls=[row for row in body['calls'] if row[0] <= stop],
                    refs=[row for row in body['refs'] if row[0] <= stop])
    args.out.mkdir(parents=True)
    for ident, binary in inventory.items():
        stock = Image(cache.data, cache.entries[ident][1])
        blob = arm64e_slice(binary.read_bytes())
        reference = Image(blob)
        imports = indirect_imports(reference)
        if typed_reference_hashes[ident] and typed_reference_hashes[ident] != {sha(binary.read_bytes())}:
            raise RuntimeError('Class-data proof belongs to a different actual reference kext')
        typed_data = dict(typed_by_owner[ident])
        # kalloc_type_view is a specified structure, not arbitrary zero-filled
        # data. Prove all scalar fields, both pointed strings and all zero
        # initializer fields, then pair only unique full descriptors.
        descriptor_groups = []
        reference_fixups = chained_import_fixups(reference)
        for image, pointers in ((stock, stock_fixups), (reference, reference_fixups)):
            descriptors = defaultdict(list)
            for section in image.sections:
                if section['name'] != '__kalloc_type':
                    continue
                if section['size'] % 64:
                    raise RuntimeError('Actual kalloc type descriptor extent differs')
                for address in range(section['va'], section['va'] + section['size'], 64):
                    raw = image.read(address, 64)
                    name, signature = pointers.get(address + 16), pointers.get(address + 32)
                    if not name or not signature or not isinstance(name[0], int) or not isinstance(signature[0], int):
                        continue
                    n, s = image.cstring(name[0]), image.cstring(signature[0])
                    if n is None or s is None:
                        continue
                    if any(raw[at:at + 8] != bytes(8) for at in (0, 8, 24, 48, 56)):
                        continue
                    flag, size = struct.unpack_from('<II', raw, 40)
                    if flag & 0x60 != 0x60:
                        continue
                    descriptors[(n, s, flag, size, name[1:], signature[1:])].append(address)
            descriptor_groups.append(descriptors)
        for key, addresses in descriptor_groups[0].items():
            targets = descriptor_groups[1].get(key, ())
            if len(addresses) == len(targets) == 1:
                typed_data[addresses[0]] = targets[0]
        left, left_shapes = bodies(stock)
        right, right_shapes = bodies(reference)
        candidates, controls, stats = [], [], Counter()
        for shape, items in left_shapes.items():
            targets = right_shapes.get(shape, ())
            if len(items) != 1 or len(targets) != 1:
                continue
            first, target = items[0], targets[0]
            a, b = left[first], right[target]
            if a['strings'] != b['strings'] or len(a['calls']) != len(b['calls']):
                continue
            calls = []
            valid = True
            for (pc, callee, link), (npc, ncallee, nlink) in zip(a['calls'], b['calls']):
                pos, npos = a['positions'].get(pc - first), b['positions'].get(npc - target)
                if pos is None or pos != npos or link != nlink:
                    valid = False
                    break
                if first <= callee < a['end']:
                    if not target <= ncallee < b['end'] or a['positions'].get(callee - first) != b['positions'].get(ncallee - target):
                        valid = False
                        break
                elif ncallee in imports['stubs']:
                    if inside(callee):
                        calls.append({'original': hex(callee), 'name': imports['stubs'][ncallee],
                                      'caller': hex(first), 'reference_caller': hex(target),
                                      'reference_stub': hex(ncallee), 'call_offset': pos,
                                      'component': ident, 'complete_caller_instructions': len(shape),
                                      'equal_strings': sorted(a['strings']),
                                      'data_operands': len(a['refs']), 'reference_data_operands': len(b['refs']),
                                      'binding_authorized': False})
                else:
                    # Keep a research call pair for later whole-component
                    # caller identity / internal dependency validation.
                    stats['internal_or_other_callee'] += 1
            if not valid:
                continue
            for row in calls:
                if int(row['original'], 16) in known:
                    controls.append({'original': row['original'], 'expected': known[int(row['original'], 16)],
                                     'caller_import_name': row['name'], 'caller': row['caller']})
            imported_data = {}
            if len(a['refs']) == len(b['refs']):
                for (_, address, kind, *_), (_, naddress, nkind, *_) in zip(a['refs'], b['refs']):
                    name = imports['pointers'].get(naddress)
                    left_pointer, right_pointer = stock_fixups.get(address), reference_fixups.get(naddress)
                    implementation = definition(name) if name else None
                    if implementation is None or kind != 'ldst' or nkind != kind or not left_pointer or not right_pointer or \
                            right_pointer[0] != name or left_pointer[1:] != right_pointer[1:] or not inside(left_pointer[0]):
                        continue
                    imported_data[(address, naddress)] = {'original': left_pointer[0], 'native': implementation, 'name': name}
            for item in {tuple(sorted(value.items())) for value in imported_data.values()}:
                item = dict(item)
                reasons = caller_issues(a, b, first, target, imports, semantic, definition,
                                       None, None, stock, reference, typed_data,
                                       typed_functions_by_owner[ident], imported_data, known_data,
                                       (item['original'], item['native']))
                if not reasons:
                    data_certificates.append({'original': hex(item['original']), 'native': hex(item['native']),
                                              'name': item['name'], 'component': ident, 'caller': hex(first),
                                              'reference_caller': hex(target),
                                              'complete_caller_shape_sha256': sha(struct.pack('<' + 'I' * len(shape), *shape)),
                                              'caller_identity_from_registered_PAC': typed_functions_by_owner[ident].get(first) == target,
                                              'import_slots': [{'original': hex(x), 'reference': hex(y)}
                                                               for (x, y), value in imported_data.items() if value == item],
                                              'proof': 'actual undefined pointer import, original resolved fixup and PAC metadata; complete safe unique caller and all other operands independently closed'})
                    if item['original'] not in known_data:
                        def data_negative(label, altered_body=b, altered_imports=imported_data):
                            if label in negatives:
                                return
                            failures = caller_issues(a, altered_body, first, target, imports, semantic, definition,
                                                     None, None, stock, reference, typed_data,
                                                     typed_functions_by_owner[ident], altered_imports, known_data,
                                                     (item['original'], item['native']))
                            if not failures:
                                raise RuntimeError('Wrong actual imported-data identity accepted: ' + label)
                            negatives[label] = {'result': 'PASS', 'component': ident, 'caller': hex(first),
                                                'original_data': hex(item['original']), 'rejections': failures}
                        altered = copy.deepcopy(imported_data)
                        for value in altered.values():
                            if value == item:
                                value['native'] += 4
                        data_negative('wrong-imported-global-definition', altered_imports=altered)
                        altered = copy.deepcopy(b)
                        for index, (left_ref, right_ref) in enumerate(zip(a['refs'], b['refs'])):
                            if imported_data.get((left_ref[1], right_ref[1])) == item:
                                value = list(right_ref)
                                value[0] += 4
                                altered['refs'][index] = tuple(value)
                                break
                        data_negative('wrong-imported-global-operand-PC', altered_body=altered)
            # Strong identity subset: leave exactly one API identity unknown;
            # every other operand must already be independently named. This
            # proves an import name, not the implementation of that API.
            for candidate in calls:
                hole, implementation = int(candidate['original'], 16), definition(candidate['name'])
                if implementation is None or not a['safe'] or not b['safe'] or len(a['refs']) != len(b['refs']):
                    continue
                proof_a, proof_b, prefix_scope = a, b, False
                def check(x=proof_a, y=proof_b, api=implementation):
                    return caller_issues(x, y, first, target, imports, semantic, definition,
                                         hole, api, stock, reference, typed_data, typed_functions_by_owner[ident],
                                         imported_data, known_data)
                reasons = check()
                if reasons and args.typed_caller_prefix and typed_functions_by_owner[ident].get(first) == target:
                    call_pc = next((pc for pc, callee, _ in a['calls']
                                    if callee == hole and a['positions'].get(pc - first) == candidate['call_offset']), None)
                    reference_pc = next((pc for pc, callee, _ in b['calls']
                                         if callee == int(candidate['reference_stub'], 16) and
                                         b['positions'].get(pc - target) == candidate['call_offset']), None)
                    left_prefix = prefix_body(stock, a, first, call_pc) if call_pc is not None else None
                    right_prefix = prefix_body(reference, b, target, reference_pc) if reference_pc is not None else None
                    if left_prefix and right_prefix:
                        prefix_reasons = caller_issues(left_prefix, right_prefix, first, target, imports, semantic,
                                                      definition, hole, implementation, stock, reference, typed_data,
                                                      typed_functions_by_owner[ident], imported_data, known_data)
                        if not prefix_reasons:
                            proof_a, proof_b, reasons, prefix_scope = left_prefix, right_prefix, [], True
                            def check(x=proof_a, y=proof_b, api=implementation):
                                return caller_issues(x, y, first, target, imports, semantic, definition,
                                                     hole, api, stock, reference, typed_data, typed_functions_by_owner[ident],
                                                     imported_data, known_data)
                if not reasons:
                    cert = dict(candidate)
                    cert.update(implementation=hex(implementation), complete_caller_shape_sha256=sha(struct.pack('<' + 'I' * len(shape), *shape)),
                                proof='complete-safe-unique-caller-all-nonhole-operands-independently-closed',
                                caller_code_sha256=sha(stock.read(first, a['end'] - first)),
                                reference_caller_code_sha256=sha(reference.read(target, b['end'] - target)))
                    cert['typed_data_operands'] = [{'original': hex(address), 'reference': hex(naddress)}
                                                  for (_, address, *_), (_, naddress, *_) in zip(a['refs'], b['refs'])
                                                  if typed_data.get(address) == naddress]
                    cert['caller_identity_from_registered_PAC'] = typed_functions_by_owner[ident].get(first) == target
                    cert['proof_scope'] = 'typed-caller-complete-entry-to-call' if prefix_scope else 'complete-caller'
                    if prefix_scope:
                        cert['boundary_bytes'] = [proof_a['end'] - first, proof_b['end'] - target]
                        cert['boundary_code_sha256'] = [sha(stock.read(first, proof_a['end'] - first)),
                                                       sha(reference.read(target, proof_b['end'] - target))]
                        if 'typed-prefix-earlier-indirect-transfer' not in negatives:
                            for label, word in (('typed-prefix-earlier-indirect-transfer', 0xd63f0100),
                                                ('typed-prefix-conditional-leaves-boundary', 0x547fffe0)):
                                mutated = bytearray(stock.data)
                                struct.pack_into('<I', mutated, stock.offset(first, 4), word)
                                if prefix_body(Image(bytes(mutated), stock.at), a, first, proof_a['end'] - 4) is not None:
                                    raise RuntimeError('Unsafe actual typed entry boundary accepted: ' + label)
                                negatives[label] = {'result': 'PASS', 'component': ident, 'caller': hex(first)}
                            changed = dict(proof_b, shape=tuple([proof_b['shape'][0] ^ 1, *proof_b['shape'][1:]]))
                            if not check(y=changed):
                                raise RuntimeError('Changed actual typed entry parameter instruction accepted')
                            negatives['typed-prefix-changed-entry-instruction'] = {'result': 'PASS', 'component': ident, 'caller': hex(first)}
                    certificates.append(cert)
                    def negative(name, changed, api=implementation):
                        if name in negatives:
                            return
                        errors = check(y=changed, api=api)
                        if not errors:
                            raise RuntimeError('Changed actual caller control accepted: ' + name)
                        negatives[name] = {'result': 'PASS', 'component': ident, 'caller': hex(first), 'rejections': errors}
                    for label, replacement in (('wrong-requested-API-wakeup-count', '_thread_wakeup_nthreads_prim'),
                                               ('wrong-requested-API-default-allocator', '_IOMalloc_external')):
                        altered = definition(replacement)
                        if altered is not None and altered != implementation:
                            negative(label, proof_b, api=altered)
                    changed = copy.deepcopy(proof_b)
                    words = list(changed['shape'])
                    words[0] ^= 1
                    changed['shape'] = tuple(words)
                    negative('changed-parameter-or-body-instruction', changed)
                    changed = copy.deepcopy(proof_b)
                    changed['safe'] = False
                    negative('unresolved-PC-relative-operand', changed)
                    if proof_b['calls']:
                        changed = copy.deepcopy(proof_b)
                        pc, address, link = changed['calls'][0]
                        changed['calls'][0] = (pc + 4, address, link)
                        negative('shifted-callsite', changed)
                    if proof_b['refs']:
                        changed = copy.deepcopy(proof_b)
                        row = list(changed['refs'][0])
                        row[1] = 0x1234567800000000
                        changed['refs'][0] = tuple(row)
                        negative('wrong-data-target', changed)
                    if len(proof_b['refs']) >= 2 and proof_b['refs'][0][1] != proof_b['refs'][1][1]:
                        changed = copy.deepcopy(proof_b)
                        x, y = list(changed['refs'][0]), list(changed['refs'][1])
                        x[1], y[1] = y[1], x[1]
                        changed['refs'][:2] = [tuple(x), tuple(y)]
                        negative('same-string-set-swapped-operand-positions', changed)
                else:
                    stats['certificate-' + reasons[0]] += 1
            candidates.extend(calls)
            stats['unique_callers'] += 1
        rows.extend(candidates)
        reports.append({'component': ident, 'binary': str(binary), 'arm64e_sha256': sha(blob),
                        'stub_imports': len(imports['stubs']), 'pointer_imports': len(imports['pointers']),
                        'stock_functions': len(left), 'reference_functions': len(right),
                        'stats': stats, 'candidate_calls': len(candidates), 'known_identity_controls': controls})
        (args.out / (ident + '.json')).write_text(json.dumps({'report': reports[-1], 'calls': candidates}, indent=2) + '\n')
        print(ident, 'unique callers', stats['unique_callers'], 'named external calls', len(candidates), flush=True)
    groups = defaultdict(list)
    for row in rows:
        groups[row['original']].append(row)
    result = {'observed_at': datetime.now().astimezone().isoformat(), 'build': '22G100',
              'reference_role': 'caller-import-names-research', 'binding_authorized': False,
              'device_access': False, 'bootable': False, 'reports': reports, 'calls': rows,
              'unique_targets': len(groups),
              'name_conflicts': {key: sorted({row['name'] for row in values}) for key, values in groups.items()
                                 if len({row['name'] for row in values}) > 1},
              'policy': 'Actual undefined import metadata at matching complete unique caller offsets; data/internal operands still require independent checks; no executable bindings emitted',
              'stock_sha256': sha(args.stock_kernel.read_bytes()), 'normalizer_sha256': sha(path.read_bytes())}
    cert_groups = defaultdict(list)
    for row in certificates:
        cert_groups[row['original']].append(row)
    admitted, refused = [], []
    for address, values in cert_groups.items():
        proposals = groups[address]
        implementations = {definition(row['name']) for row in proposals}
        components = {row['component'] for row in values}
        shapes = {row['complete_caller_shape_sha256'] for row in values}
        typed_caller = any(row.get('caller_identity_from_registered_PAC') for row in values)
        if len(implementations) != 1 or None in implementations or not (
                len(components) >= 2 and len(shapes) >= 2 or typed_caller):
            refused.append({'original': address, 'components': len(components), 'shapes': len(shapes),
                            'reason': 'need unambiguous actual source aliases plus two caller shapes/components or independently registered full-PAC caller identity'})
            continue
        implementation = next(iter(implementations))
        if int(address, 16) in semantic and semantic[int(address, 16)] != implementation:
            raise RuntimeError('Caller-origin import contradicts an independent API identity: ' + address)
        chosen = min(values, key=lambda row: (row['name'], row['component'], row['caller']))
        admitted.append({'original': address, 'name': chosen['name'],
                         'evidence': 'Actual undefined import symbol at identical normalized callsite in complete safe unique callers, or complete entry-to-call boundaries in independently registered typed caller methods; every preceding noncandidate callee and data operand independently closed; boundary rejects earlier indirect transfers and local branches leaving the slice; independent typed caller identity or two full caller shapes/components; all observations resolve through actual source aliases to one definition',
                         'independent_support': 'registered-typed-caller-and-complete-operands' if typed_caller else 'two-components-two-complete-callers',
                         'identity_proof_kind': 'caller-origin-complete-v1', 'certificates': values,
                         'identity_only': True, 'runtime_qualified': False})
    data_groups = defaultdict(list)
    for row in data_certificates:
        data_groups[row['original']].append(row)
    admitted_data = []
    for address, values in data_groups.items():
        targets = {row['native'] for row in values}
        typed = any(row['caller_identity_from_registered_PAC'] for row in values)
        if len(targets) != 1 or not (typed or len({row['component'] for row in values}) >= 2 and
                                    len({row['complete_caller_shape_sha256'] for row in values}) >= 2):
            continue
        target = next(iter(targets))
        if int(address, 16) in known_data and known_data[int(address, 16)] != int(target, 16):
            raise RuntimeError('Caller-origin data identity conflicts with independent global contract')
        admitted_data.append({'original': address, 'name': min(row['name'] for row in values),
                              'evidence': 'Actual KDK undefined pointer-import symbol and stock resolved GOT fixup agree at matching complete safe caller operand position and PAC metadata; all other calls/data independently closed; two different callers/components or independent registered typed caller identity; no zero-initializer or pointer-order naming',
                              'identity_proof_kind': 'caller-origin-imported-data-v1', 'certificates': values,
                              'runtime_qualified': False})
    identity = {'build': '22G100', 'arch': 'arm64e', 'stock_sha256': result['stock_sha256'],
                'native_sha256': sha(native_path.read_bytes()), 'functions': admitted, 'data': admitted_data,
                'data_certificates': data_certificates,
                'device_access': False, 'bootable': False, 'certificates': certificates, 'refused': refused,
                'negative_controls': negatives,
                'qualified_seed_map_sha256': sha(args.qualified_map.read_bytes()),
                'registered_data_reference_sha256': sha(args.registered_data_reference.read_bytes()) if args.registered_data_reference else None,
                'policy': 'API identity only; does not claim allocator implementation or device runtime behavior'}
    (args.out / 'identity-anchors.json').write_text(json.dumps(identity, indent=2) + '\n')
    (args.out / 'research.json').write_text(json.dumps(result, indent=2) + '\n')
    (args.out / Path(__file__).name).write_bytes(Path(__file__).read_bytes())
    print('Caller-origin research targets', len(groups), 'name conflicts', len(result['name_conflicts']),
          'strict certificates', len(certificates), 'API identities', len(admitted),
          'data certificates', len(data_certificates), 'data identities', len(admitted_data), flush=True)


if __name__ == '__main__':
    main()
