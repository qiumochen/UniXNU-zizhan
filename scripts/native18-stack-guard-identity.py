#!/usr/bin/env python3
"""Prove the compiler stack-canary global using paired load/check/fatal paths.

Use the actual KDK undefined guard import and stack-check-fail stub. The stock
caller must store the loaded guard in its frame, reload that same frame slot,
compare against the same global and branch to the independently known fatal
API. No global is named by initial value, address order or graph propagation.
"""
import argparse
from collections import defaultdict
from datetime import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('prelink-kit', 'stock-kernel', 'qualified-map', 'caller-research', 'pointer-research', 'out'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Keep old evidence; use a new private output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan
    from fileset_shape import checked_shape
    path = Path(__file__).with_name('native18-kext-import-names.py')
    spec = importlib.util.spec_from_file_location('guard_import_reader', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    cache = Image(args.stock_kernel.read_bytes())
    fixups = cache.fixups()
    pairing = json.loads(args.qualified_map.read_text())
    receipt = json.loads(args.qualified_map.with_name('receipt.json').read_text())
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    if receipt['stock_sha256'] != digest(args.stock_kernel):
        parser.error('Independent map belongs to another stock kernel')
    native_path = Path(pairing['native'])
    if receipt['native_sha256'] != digest(native_path):
        parser.error('Independent map native kernel changed')
    native = Image(native_path.read_bytes())
    definitions = [a for a, ns in native.symbols().items() if '___stack_chk_guard' in ns]
    if len(definitions) != 1:
        parser.error('Require unique actual source stack-guard definition')
    research = json.loads(args.caller_research.read_text())
    if research['stock_sha256'] != receipt['stock_sha256']:
        parser.error('Caller research belongs to different stock input')
    binaries = {r['component']: Path(r['binary']) for r in research['reports']}
    pointers = json.loads(args.pointer_research.read_text())
    proposals = {(r['component'], int(r['caller'], 16), int(r['reference_caller'], 16))
                 for r in pointers if r['name'] == '___stack_chk_guard'}
    loaded, proofs, negatives = {}, [], []
    def ends(image):
        starts = sorted(image.function_starts())
        return dict(zip(starts, starts[1:]))
    def guards(image, first, end, imports=None):
        raw = image.read(first, end - first)
        shape, positions, calls, refs = checked_shape(raw, first)
        refmap = {pc: (target, kind) for pc, target, kind in refs}
        words = struct.unpack('<' + 'I' * (len(raw) // 4), raw)
        found = []
        # Exact compiler entry sequence uses x8 and [fp-8]. Limit it to the
        # actual prologue; never infer a saved-canary field from a string.
        for i in range(1, min(16, len(words) - 3)):
            block = words[i:i + 4]
            if block[0] & 0x9f00001f != 0x90000008 or block[1] & 0xffc003ff != 0xf9400108 or \
                    block[2:] != (0xf9400108, 0xf81f83a8):
                continue
            operand = refmap.get(first + (i + 1) * 4)
            if not operand or operand[1] != 'ldst':
                continue
            for j in range(i + 4, len(words) - 8):
                tail = words[j:j + 9]
                if tail[0] != 0xf85f83a8 or tail[1] & 0x9f00001f != 0x90000009 or \
                        tail[2] & 0xffc003ff != 0xf9400129 or tail[3:5] != (0xf9400129, 0xeb08013f) or \
                        tail[5] & 0xff00001f != 0x54000001 or tail[8] != 0xd65f0fff:
                    continue
                other = refmap.get(first + (j + 2) * 4)
                if other != operand:
                    continue
                signed19 = (tail[5] >> 5) & 0x7ffff
                if signed19 & 0x40000:
                    signed19 -= 0x80000
                fail_pc = first + (j + 5) * 4 + signed19 * 4
                fatal = [callee for pc, callee, link in calls if pc == fail_pc and link]
                if len(fatal) != 1:
                    continue
                if imports is not None and imports['stubs'].get(fatal[0]) != '___stack_chk_fail':
                    continue
                found.append({'operand': operand[0], 'fail': fatal[0], 'entry_pc': (i + 1) * 4,
                              'check_pc': (j + 2) * 4, 'shape': tuple(shape),
                              'positions': positions, 'code_sha256': hashlib.sha256(raw).hexdigest(),
                              'entry_words': block, 'check_words': tail})
        return found
    for owner, first, target in sorted(proposals):
        if owner not in loaded:
            ref = Image(module.arm64e_slice(binaries[owner].read_bytes()))
            stock = Image(cache.data, cache.entries[owner][1])
            loaded[owner] = stock, ref, ends(stock), ends(ref), module.indirect_imports(ref), module.chained_import_fixups(ref)
        stock, ref, old_ends, ref_ends, imports, ref_fixups = loaded[owner]
        if first not in old_ends or target not in ref_ends:
            continue
        left = guards(stock, first, old_ends[first])
        right = guards(ref, target, ref_ends[target], imports)
        if len(left) != 1 or len(right) != 1:
            continue
        a, b = left[0], right[0]
        if a['shape'] != b['shape'] or (a['entry_pc'], a['check_pc']) != (b['entry_pc'], b['check_pc']):
            continue
        original = fixups.get(a['operand'])
        reference_pointer = ref_fixups.get(b['operand'])
        if not original or imports['pointers'].get(b['operand']) != '___stack_chk_guard' or \
                not reference_pointer or reference_pointer[0] != '___stack_chk_guard' or original[1:] != reference_pointer[1:]:
            continue
        known = pairing['functions'].get(hex(a['fail']))
        if not known or known['name'] != '___stack_chk_fail' or not known['evidence'].startswith('Complete actual stack_protector.c fatal wrapper'):
            continue
        proofs.append({'component': owner, 'caller': hex(first), 'reference_caller': hex(target),
                       'original': hex(original[0]), 'import_name': '___stack_chk_guard',
                       'entry_pc': a['entry_pc'], 'check_pc': a['check_pc'],
                       'fatal_target': hex(a['fail']), 'stock_code_sha256': a['code_sha256'],
                       'reference_code_sha256': b['code_sha256']})
        if not negatives:
            for label, pc in (('wrong-saved-frame-slot', first + a['entry_pc'] + 8),
                              ('wrong-restored-frame-slot', first + a['check_pc'] - 8)):
                altered = bytearray(stock.data)
                offset = stock.offset(pc, 4)
                word, = struct.unpack_from('<I', altered, offset)
                struct.pack_into('<I', altered, offset, word ^ (1 << 12))
                if guards(Image(bytes(altered), stock.at), first, old_ends[first]):
                    raise RuntimeError('Wrong actual canary frame slot accepted')
                negatives.append({'name': label, 'result': 'PASS'})
            changed_imports = {key: dict(value) for key, value in imports.items()}
            changed_imports['stubs'][b['fail']] = '_thread_wakeup_prim'
            if guards(ref, target, ref_ends[target], changed_imports):
                raise RuntimeError('Wrong canary fatal API accepted')
            negatives.append({'name': 'wrong-fatal-import-name', 'result': 'PASS'})
    addresses = {row['original'] for row in proofs}
    if len(addresses) != 1 or len({row['component'] for row in proofs}) < 2 or len(proofs) < 4:
        raise RuntimeError('Insufficient independent compiler canary load/check/fatal witnesses')
    result = {'build': '22G100', 'arch': 'arm64e', 'stock_sha256': receipt['stock_sha256'],
              'native_sha256': receipt['native_sha256'], 'functions': [],
              'data': [{'original': next(iter(addresses)), 'name': '___stack_chk_guard',
                        'evidence': 'Multiple actual independent compiler stack-protected callers load KDK named undefined guard import into x8 and [fp-8], reload same frame slot and same original resolved GOT target, compare and conditional-branch to independently proven real stack_chk_fail API; complete normalized caller shape and exact entry/check PCs agree; no initializer/order inference'}],
              'proofs': proofs, 'negative_controls': negatives, 'observed_at': datetime.now().astimezone().isoformat(),
              'device_access': False, 'bootable': False,
              'runtime_guard_randomness_qualified': False}
    args.out.mkdir(parents=True)
    (args.out / 'identity-anchors.json').write_text(json.dumps(result, indent=2) + '\n')
    (args.out / Path(__file__).name).write_bytes(Path(__file__).read_bytes())
    print('Real compiler guard identity:', len(proofs), 'callers /', len({r['component'] for r in proofs}), 'components')


if __name__ == '__main__':
    main()
