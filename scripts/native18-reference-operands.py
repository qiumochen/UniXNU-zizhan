#!/usr/bin/env python3
"""Verify complete symbol-reference bodies and every relocated operand in batches.

Unique PAC-shell shapes are proposals only. Calls and data operands must resolve
to independent direct/class contracts, exact strings, or bodies closed in an
earlier round. Order/graph proposals and operationally equivalent API aliases
cannot seed identities. This writes names and audit evidence, never a collection.
"""
import argparse
from collections import Counter, defaultdict
from datetime import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('native-source', 'prelink-kit', 'stock-kernel', 'native-kernel',
                'reference-kernel', 'qualified-map', 'out'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--reference-dwarf', type=Path,
                        help='UUID-matched KDK dSYM; omit for a fully symbol-bearing source reference')
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Preserve prior evidence; use a new output directory')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan, signed
    shell = load_module('reference_shell', args.native_source / 'scripts/derive-pong-pac-shell-names.py').shell
    reference_module = load_module('reference_metadata', Path(__file__).with_name('native18-kdk-reference.py'))
    metadata = reference_module.metadata
    for path, pin in ((args.native_source, reference_module.PROFILE['source']['commit']),
                      (args.prelink_kit, reference_module.PROFILE['prelink']['commit'])):
        if subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip() != pin:
            parser.error('Wrong pinned analysis source')
    stock_blob, native_blob, ref_blob = [path.read_bytes() for path in
        (args.stock_kernel, args.native_kernel, args.reference_kernel)]
    dwarf_blob = args.reference_dwarf.read_bytes() if args.reference_dwarf else ref_blob
    meta, dwarf = metadata(ref_blob), metadata(dwarf_blob)
    if (meta['kind'], dwarf['kind']) != (2, 10 if args.reference_dwarf else 2) or \
            any(meta[key] != dwarf[key] for key in ('uuid', 'cpu', 'subtype')):
        parser.error('Reference code and dSYM identity mismatch')
    if any(not names <= dwarf['symbols'].get(address, set()) for address, names in meta['symbols'].items()):
        parser.error('dSYM contradicts reference exports')
    cache, native, reference = Image(stock_blob), Image(native_blob), Image(ref_blob)
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    qualified = json.loads(args.qualified_map.read_text())
    receipt = json.loads(args.qualified_map.with_name('receipt.json').read_text())
    if qualified.get('qualification') != 'INDEPENDENT_CONTRACTS_ONLY' or \
            (receipt['stock_sha256'], receipt['native_sha256']) != (sha(args.stock_kernel), sha(args.native_kernel)):
        parser.error('Independent seed map belongs to different actual inputs')
    ref_names = defaultdict(set)
    for address, names in dwarf['symbols'].items():
        for name in names:
            ref_names[name].add(address)
    native_symbols = native.symbols()
    accepted, data = {}, {}
    seed_rejections = Counter()
    for kind, group in (('functions', accepted), ('data', data)):
        for key, row in qualified[kind].items():
            prior_complete_body = kind == 'functions' and row['how'] == 'qualified-exact-reference' and \
                row.get('evidence', '').startswith(
                    'Unique bidirectional complete instruction body with normalized compiler PAC checks;')
            if not (row['how'] in ('qualified-direct-contract', 'qualified-complete-class-pac') or prior_complete_body) or \
                    (kind == 'functions' and not row.get('semantic_identity')) or row.get('review_only'):
                seed_rejections['not-independent-semantic-contract'] += 1
                continue
            names = native_symbols.get(int(row['native'], 16), [])
            targets = {address for name in names for address in ref_names[name]}
            if len(targets) == 1:
                group[int(key, 16)] = next(iter(targets))
            else:
                seed_rejections['reference-name-absent-or-ambiguous'] += 1
    seed_counts = {'functions': len(accepted), 'data': len(data)}

    def bodies(image):
        ranges = [(s['va'], s['va'] + s['filesize']) for s in image.segments
                  if s['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and s['filesize']]
        starts = sorted(a for a in image.function_starts() if any(lo <= a < hi for lo, hi in ranges))
        result, shapes = {}, defaultdict(list)
        rejected = Counter()
        for index, start in enumerate(starts):
            end = min([hi for lo, hi in ranges if lo <= start < hi] +
                      ([starts[index + 1]] if index + 1 < len(starts) else []))
            if end - start > 65536:
                rejected['oversize'] += 1
                continue
            value = shell(image, start, end)
            if value is None:
                rejected['no-shell'] += 1
                continue
            shape, strings, positions = value
            # Uniqueness includes unsafe bodies too; excluding one must never
            # turn an ambiguous shape into a unique proof candidate.
            shapes[shape].append(start)
            code = image.read(start, end - start)
            calls, refs, _ = scan(code, start, with_origins=True)
            consumed = {row[3] for row in refs if row[3] is not None}
            unsafe = None
            for offset in range(0, len(code), 4):
                word, = struct.unpack_from('<I', code, offset)
                if word & 0x9f000000 == 0x90000000 and start + offset not in consumed:
                    unsafe = 'unresolved-adrp'
                if word & 0x3b000000 == 0x18000000:
                    unsafe = 'literal-load-not-proved'
                bits = 19 if word & 0xff000010 == 0x54000000 or word & 0x7e000000 == 0x34000000 else \
                    14 if word & 0x7e000000 == 0x36000000 else 0
                if bits:
                    destination = start + offset + signed((word >> 5) & ((1 << bits) - 1), bits) * 4
                    if not start <= destination < end:
                        unsafe = 'external-conditional-branch'
            if any(pc - start not in positions for pc, *_ in calls + refs):
                unsafe = 'operand-in-removed-instruction'
            # ADRP is consumed by the precise ADD/LDST recorded by scan. No
            # unresolved page, literal, or out-of-body conditional is masked.
            if unsafe:
                rejected[unsafe] += 1
                continue
            row = {'end': end, 'shape': shape, 'strings': strings, 'positions': positions,
                   'calls': calls, 'refs': refs}
            result[start] = row
        return result, shapes, rejected

    left, left_shapes, left_rejections = bodies(original)
    right, right_shapes, right_rejections = bodies(reference)
    proposals, names = {}, {}
    for shape, firsts in left_shapes.items():
        targets = right_shapes.get(shape, ())
        if len(firsts) != 1 or len(targets) != 1:
            continue
        first, target = firsts[0], targets[0]
        if first not in left or target not in right:
            continue
        if left[first]['strings'] != right[target]['strings']:
            continue
        found = sorted(name for name in dwarf['symbols'].get(target, ()) if len(ref_names[name]) == 1
                       and not any(part in name for part in ('.cold.', '.llvm.', 'OUTLINED_FUNCTION')))
        if found:
            proposals[first], names[first] = target, found

    def check(first, target, altered_calls=None, altered_refs=None):
        a, b = left[first], right[target]
        if a['shape'] != b['shape']:
            return 'body'
        def pos(row, pc, base):
            return row['positions'].get(pc - base)
        def operand(x, y):
            if first <= x < a['end']:
                return target <= y < b['end'] and pos(a, x, first) is not None and \
                    pos(a, x, first) == pos(b, y, target)
            return accepted.get(x) == y or data.get(x) == y
        calls = b['calls'] if altered_calls is None else altered_calls
        refs = b['refs'] if altered_refs is None else altered_refs
        if len(a['calls']) != len(calls) or len(a['refs']) != len(refs):
            return 'operand-count'
        for (pc, callee, link), (npc, ncallee, nlink) in zip(a['calls'], calls):
            if pos(a, pc, first) != pos(b, npc, target) or link != nlink:
                return 'call-position-kind'
            if not operand(callee, ncallee):
                return 'unclosed-callee'
        for (pc, address, kind, origin, width), (npc, naddress, nkind, norigin, nwidth) in zip(a['refs'], refs):
            if pos(a, pc, first) != pos(b, npc, target) or (kind, width) != (nkind, nwidth):
                return 'data-position-kind'
            if (origin is None) != (norigin is None) or origin is not None and \
                    pos(a, origin, first) != pos(b, norigin, target):
                return 'data-page-producer'
            if operand(address, naddress):
                continue
            literal = original.cstring(address)
            if literal is None or literal != reference.cstring(naddress):
                return 'unclosed-data'
        return None

    closed, contradictions, rounds = {}, [], []
    pending = set(proposals)
    for index in range(64):
        added = []
        for first in sorted(pending):
            target = proposals[first]
            if first in accepted and accepted[first] != target:
                contradictions.append({'original': hex(first), 'contract': hex(accepted[first]), 'proposal': hex(target)})
                continue
            if check(first, target) is None:
                added.append(first)
        for first in added:
            accepted[first] = proposals[first]
            closed[first] = index
        pending.difference_update(added)
        rounds.append(len(added))
        if not added:
            break
    controls = []
    for kind in ('call', 'data'):
        for first in sorted(closed):
            target = proposals[first]
            rows = right[target]['calls' if kind == 'call' else 'refs']
            for index, row in enumerate(rows):
                if kind == 'call' and first <= left[first]['calls'][index][1] < left[first]['end']:
                    continue
                changed = list(rows)
                bad = list(row)
                bad[1] = 0x1234567800000000
                changed[index] = tuple(bad)
                failure = check(first, target, **{'altered_calls' if kind == 'call' else 'altered_refs': changed})
                if failure is None:
                    raise RuntimeError('Wrong operand negative control was accepted')
                controls.append({'kind': kind, 'original': hex(first), 'result': 'PASS', 'rejection': failure})
                break
            if any(row['kind'] == kind for row in controls):
                break
    evidence = ('Unique bidirectional complete instruction body with normalized compiler PAC checks; '
                'every external call and ADR/ADRP operand closed against independent semantic contracts, '
                'exact literal strings or prior complete-body rounds; no order/graph dependency seeds')
    rows = [{'original': hex(first), 'names': names[first], 'instructions': len(left[first]['shape']),
             'operand_proof': 'complete-v1', 'round': closed[first], 'evidence': evidence}
            for first in sorted(closed)]
    args.out.mkdir(parents=True)
    (args.out / Path(__file__).name).write_bytes(Path(__file__).read_bytes())
    shell_path = args.native_source / 'scripts/derive-pong-pac-shell-names.py'
    (args.out / shell_path.name).write_bytes(shell_path.read_bytes())
    result = {'build': '22G100', 'reference_role': 'names-only', 'analysis_only': True,
              'reference': str(args.reference_kernel), 'functions': rows,
              'operand_proof': 'complete-v1', 'stock_sha256': sha(args.stock_kernel),
              'dependency_native_sha256': sha(args.native_kernel)}
    (args.out / 'names.json').write_text(json.dumps(result, indent=2) + '\n')
    report = {'observed_at': datetime.now().astimezone().isoformat(), 'result': 'BATCH_OPERAND_CHECK_FINISHED',
              'seed_counts': seed_counts, 'seed_rejections': seed_rejections,
              'unique_safe_proposals': len(proposals), 'closed': len(closed), 'rounds': rounds,
              'pending_reasons': Counter(check(first, proposals[first]) for first in pending),
              'body_rejections': {'stock': left_rejections, 'reference': right_rejections},
              'independent_contract_contradictions': contradictions, 'negative_controls': controls,
              'reference_uuid': meta['uuid'], 'bootable': False, 'collection_written': False,
              'device_access': False,
              'inputs': [{'path': str(path.resolve()), 'sha256': sha(path)} for path in
                         (args.stock_kernel, args.native_kernel, args.reference_kernel,
                          args.qualified_map, Path(__file__), shell_path, args.reference_dwarf) if path]}
    (args.out / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('result', 'seed_counts', 'unique_safe_proposals',
                                                 'closed', 'rounds', 'pending_reasons', 'negative_controls')}), flush=True)


if __name__ == '__main__':
    main()
