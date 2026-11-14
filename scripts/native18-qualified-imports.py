#!/usr/bin/env python3
"""Separate provisional matcher candidates from independently qualified imports.

Graph/string/order propagation is useful research, not a proof of KPI ABI.
This emits a separate closed-evidence map seeded by actual class/anchor and
exact-reference identities, with recursive exact instruction/operand proofs.
No kernel bytes, missing stubs or relocated collections are emitted.
"""
import argparse
from collections import defaultdict
from datetime import datetime
import hashlib
import json
from pathlib import Path
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('prelink-kit', 'stock-kernel', 'native-kernel', 'pairing', 'exact-names', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--parent', type=Path, action='append', default=[])
    parser.add_argument('--anchor', type=Path, action='append', default=[])
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain prior qualification evidence; choose a new output')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image, scan, signed
    from fileset_shape import checked_shape
    from fileset_match import Matcher
    from fileset_emit import dependency_closure
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    original = Image(cache.data, cache.entries['com.apple.kernel'][1])
    candidate = json.loads(args.pairing.read_text())
    if (candidate.get('build'), candidate.get('arch')) != ('22G100', 'arm64e') or Path(candidate['original']).resolve() != args.stock_kernel.resolve() or Path(candidate['native']).resolve() != args.native_kernel.resolve():
        parser.error('Require the actual-input provisional pairing')
    names = defaultdict(set)
    symbols = native.symbols()
    for address, rows in symbols.items():
        for name in rows:
            names[name].add(address)
    def named(name):
        if len(names.get(name, ())) != 1:
            return None
        return next(iter(names[name]))
    functions, data, sources = {}, {}, []
    rejected = []
    def function(address, target, evidence, kind, identity=True):
        key = hex(address)
        found = functions.get(key)
        if found and int(found['native'], 16) != target:
            if found['semantic_identity'] and not identity:
                rejected.append({'original': key, 'kind': 'operational-alias-withheld-for-proven-API-identity',
                                 'withheld_native': hex(target), 'identity_native': found['native']})
                return
            if not found['semantic_identity'] and identity:
                rejected.append({'original': key, 'kind': 'proven-API-identity-replaces-operational-alias',
                                 'previous_native': found['native'], 'identity_native': hex(target)})
            else:
                raise RuntimeError('Independent qualified function contracts disagree: ' + key)
        # Provisional proposals may be wrong; replace them from actual proof
        # and record the contradiction, never let first-match order win.
        proposal = candidate['functions'].get(key)
        if proposal and int(proposal['native'], 16) != target:
            rejected.append({'original': key, 'kind': 'qualified-function-contradicts-provisional',
                             'provisional': proposal, 'qualified_native': hex(target), 'proof': kind})
        functions[key] = {'native': hex(target), 'name': min(symbols.get(target, ['?'])), 'addend': 0,
                          'how': 'qualified-' + kind, 'evidence': evidence, 'semantic_identity': identity}
    def datum(address, target, evidence, kind):
        key = hex(address)
        found = data.get(key)
        if found and int(found['native'], 16) != target:
            raise RuntimeError('Independent qualified data contracts disagree: ' + key)
        data[key] = {'native': hex(target), 'name': min(symbols.get(target, ['?'])),
                     'how': 'qualified-' + kind, 'evidence': evidence}
    stock_hash, native_hash = sha(args.stock_kernel), sha(args.native_kernel)
    exact = json.loads(args.exact_names.read_text())
    if exact.get('build') != '22G100' or exact.get('reference_role') != 'names-only':
        parser.error('Require qualified exact-reference name selector output')
    for row in exact['functions']:
        targets = {named(name) for name in row['names']}
        complete_operands = row.get('operand_proof') == 'complete-v1' and row.get('evidence', '').startswith(
            'Unique bidirectional complete instruction body with normalized compiler PAC checks;')
        if not row.get('review_only') and row.get('binding_authorized') is not False and \
                None not in targets and len(targets) == 1 and row.get('supporting_exact_references') and \
                (row.get('evidence', '').startswith('Unique bidirectional exact relocated-instruction body') or complete_operands):
            function(int(row['original'], 16), next(iter(targets)), row['evidence'], 'exact-reference')
    sources.append({'path': str(args.exact_names), 'sha256': sha(args.exact_names)})
    for path in args.parent:
        parent = json.loads(path.read_text())
        if not parent.get('abi_compatible') or (parent.get('build'), parent.get('arch')) != ('22G100', 'arm64e'):
            parser.error('Only complete actual PAC-compatible parents may seed qualification')
        if parent.get('stock_sha256') and (parent['stock_sha256'], parent.get('native_sha256')) != (stock_hash, native_hash):
            parser.error('Core class qualification belongs to a different kernel')
        for row in parent['methods']:
            target = named(row['name'])
            if target is not None and original.section(int(row['address'], 16)):
                # Methods supplied by retained stock kexts are not native
                # kernel function definitions and are left to that closure.
                section = original.section(int(row['address'], 16))
                if section['segment'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST'):
                    function(int(row['address'], 16), target, parent['evidence'], 'complete-class-pac')
        classname = parent['classname']
        for field, symbol in (('vtable', '__ZTV' + str(len(classname)) + classname),
                              ('metaclass', '__ZN' + str(len(classname)) + classname + '10gMetaClassE')):
            target = named(symbol)
            if field in parent and target is not None:
                datum(int(parent[field], 16), target, parent['evidence'], 'complete-class-pac')
        sources.append({'path': str(path), 'sha256': sha(path)})
    direct_page_geometry = False
    pending_operand_contracts = []
    for path in args.anchor:
        anchor = json.loads(path.read_text())
        if (anchor.get('stock_sha256'), anchor.get('native_sha256')) != (stock_hash, native_hash):
            parser.error('Direct anchor qualification belongs to different actual inputs')
        if anchor.get('review_only') or anchor.get('binding_authorized') is False:
            parser.error('Review-only anchor is not an executable contract')
        for row in anchor.get('functions', []):
            if row.get('review_only') or row.get('binding_authorized') is False:
                rejected.append({'original': row['original'], 'kind': 'review-only-function-withheld'})
                continue
            target = named(row['name'])
            if target is None or not row.get('evidence'):
                parser.error('Direct function proof must name a unique actual source definition')
            if row.get('operational_equivalence_only'):
                # Producer maps are research inputs. A complete code wrapper
                # cannot borrow a guessed callee/global identity and then
                # bootstrap that guess into this independent proof set.
                pending_operand_contracts.append((row, target))
            else:
                # Final placement policy and API identity are separate. The
                # boolean wakeup proof deliberately binds late, but its real
                # bool/count counterexamples lock the API, not an arbitrary
                # operation-equivalent implementation. Preserve that fact
                # for older receipts as well as the explicit producer field.
                wakeup_contract = row['name'] == '_thread_wakeup_prim' and \
                    anchor.get('boundary_replay', {}).get('result') == 'PASS' and \
                    anchor.get('boundary_replay', {}).get('cases') == 768 and \
                    len(anchor.get('boundary_replay', {}).get('wrong_api_controls', [])) == 3
                identity = not row.get('final_import_only') or row.get('api_identity_proven') or wakeup_contract
                function(int(row['original'], 16), target, row['evidence'], 'direct-contract', identity)
        for row in anchor.get('data', []):
            if row.get('review_only') or row.get('binding_authorized') is False:
                rejected.append({'original': row['original'], 'kind': 'review-only-data-withheld'})
                continue
            target = named(row['name'])
            if target is None or not row.get('evidence'):
                parser.error('Direct data proof must name a unique actual source definition')
            datum(int(row['original'], 16), target + row.get('addend', 0), row['evidence'], 'direct-contract')
        if anchor.get('page_geometry_direct_store_contract', {}).get('qualified'):
            if {row['name'] for row in anchor['data']} != {'_PAGE_SHIFT_CONST', '_page_shift_user32'} or anchor['page_geometry_direct_store_contract'].get('native_shift') != 12:
                raise RuntimeError('Direct page-store proof must cover both actual native4KiB variables')
            direct_page_geometry = True
        sources.append({'path': str(path), 'sha256': sha(path)})
    # The planner independently re-derives these semantic page globals from
    # the actual source/stock code. Keep their exact explicit evidence.
    arm_init_qualified = any(row['name'] == '_arm_init' and row['semantic_identity'] for row in functions.values())
    for row in candidate.get('native_page_globals', []):
        if arm_init_qualified and row.get('evidence'):
            datum(int(row['original'], 16), int(row['native'], 16), row['evidence'], 'page-geometry')
    def bodies(image):
        starts = sorted(a for a in image.function_starts() if any(row['name'] in ('__TEXT_EXEC', '__TEXT_BOOT_EXEC', '__KLD', '__LAST') and row['va'] <= a < row['va'] + row['filesize'] for row in image.segments))
        result = {}
        for first, last in zip(starts, starts[1:]):
            if last - first > 65536:
                continue
            code = image.read(first, last - first)
            shape, positions, calls, refs = checked_shape(code, first)
            # The stock/source need not have the same static placement,
            # but no unresolved PC-relative operand is dropped as a shape.
            _, detailed, _ = scan(code, first, with_origins=True)
            used = {item[3] for item in detailed if item[3] is not None}
            safe = True
            import struct
            for offset in range(0, len(code), 4):
                word, = struct.unpack_from('<I', code, offset)
                if word & 0x9f000000 == 0x90000000 and first + offset not in used:
                    safe = False
                if word & 0x3b000000 == 0x18000000:
                    safe = False
                bits = 19 if word & 0xff000010 == 0x54000000 or word & 0x7e000000 == 0x34000000 else 14 if word & 0x7e000000 == 0x36000000 else 0
                if bits:
                    destination = first + offset + signed((word >> 5) & ((1 << bits) - 1), bits) * 4
                    if not first <= destination < last:
                        safe = False
            if not safe:
                continue
            result[first] = {'code': code, 'shape': tuple(shape), 'positions': positions,
                             'calls': calls, 'refs': refs, 'end': last}
        return result
    left, right = bodies(original), bodies(native)
    # The research matcher can propose a different duplicate body on another
    # run. Its proposal must not determine whether an independent proof is
    # discoverable. Search actual complete bodies, retaining all operand
    # checks below; equal-effect implementations never acquire API identity.
    right_shapes = defaultdict(list)
    for address, body in right.items():
        right_shapes[body['shape']].append(address)
    def target_order(address):
        return min(symbols.get(address, ['~'])), address
    for targets in right_shapes.values():
        targets.sort(key=target_order)
    def operands(first, target):
        a, b = left.get(first), right.get(target)
        if not a or not b or a['shape'] != b['shape'] or len(a['calls']) != len(b['calls']) or len(a['refs']) != len(b['refs']):
            return False
        def position(row, pc, base):
            return row['positions'].get(pc - base)
        for (pc, callee, link), (npc, ncallee, nlink) in zip(a['calls'], b['calls']):
            if position(a, pc, first) != position(b, npc, target) or link != nlink:
                return False
            if first <= callee < a['end']:
                if not target <= ncallee < b['end'] or position(a, callee, first) != position(b, ncallee, target):
                    return False
            elif hex(callee) not in functions or int(functions[hex(callee)]['native'], 16) != ncallee:
                return False
        for (pc, operand, kind), (npc, noperand, nkind) in zip(a['refs'], b['refs']):
            if position(a, pc, first) != position(b, npc, target) or kind != nkind:
                return False
            known = data.get(hex(operand))
            if known and int(known['native'], 16) == noperand:
                continue
            literal = original.cstring(operand)
            if literal is None or literal != native.cstring(noperand):
                return False
        return True
    # These leaves have no memory, calls or branches. Build their pool from
    # the actual images too, instead of depending on a graph's pure-leaf tag.
    from types import SimpleNamespace
    sides = [SimpleNamespace(functions={address: {'end': row['end']} for address, row in values.items()}, image=image)
             for values, image in ((left, original), (right, native))]
    pure_targets = defaultdict(list)
    for address in right:
        value = Matcher.pure_leaf(sides[1], address)
        if value is not None:
            pure_targets[value].append(address)
    for targets in pure_targets.values():
        targets.sort(key=target_order)
    for address in sorted(left):
        if hex(address) in functions:
            continue
        value = Matcher.pure_leaf(sides[0], address)
        if value is not None and pure_targets.get(value):
            function(address, pure_targets[value][0], 'Complete pure computation leaf has equal register/return instructions without memory or external references', 'pure-computation', False)
    rounds = []
    equivalent_bodies = {}
    for index in range(32):
        added = 0
        still_pending = []
        for row, target in pending_operand_contracts:
            valid = True
            for contract in row.get('contracts', []):
                kind = contract['kind']
                if kind in ('mapped-callee', 'mapped-data'):
                    group = functions if kind == 'mapped-callee' else data
                    bound = group.get(contract['original'])
                    if not bound or bound['native'] != contract['native']:
                        valid = False
                        break
                elif kind not in ('identical-string', 'complete-byte-identical-local-body'):
                    valid = False
                    break
            if valid and row.get('contracts'):
                function(int(row['original'], 16), target, row['evidence'], 'closed-operational-contract', False)
                added += 1
            else:
                still_pending.append((row, target))
        pending_operand_contracts = still_pending
        for first in sorted(left):
            if hex(first) in functions:
                continue
            targets = [target for target in right_shapes.get(left[first]['shape'], ()) if operands(first, target)]
            if targets:
                function(first, targets[0], 'Complete checked instruction body and every external call/data operand recursively proved from independently qualified contracts', 'recursive-exact-operands', False)
                if len(targets) > 1:
                    equivalent_bodies[hex(first)] = [hex(target) for target in targets]
                added += 1
        rounds.append(added)
        if not added:
            break
    result = dict(candidate)
    result.update(functions=functions, data=data, qualification='INDEPENDENT_CONTRACTS_ONLY',
                  bootable=False, analysis_only=True, graph_candidates_trusted=False)
    result['stats'] = {'qualified_functions': len(functions), 'qualified_data': len(data),
                       'provisional_functions': len(candidate['functions']), 'provisional_data': len(candidate['data'])}
    args.out.mkdir(parents=True)
    (args.out / 'pairing.json').write_text(json.dumps(result, indent=2) + '\n')
    audit = {'observed_at': datetime.now().astimezone().isoformat(), 'result': 'QUALIFIED_MAP_REQUIRES_CLOSURE_RECHECK',
             'stock_sha256': stock_hash, 'native_sha256': native_hash, 'candidate_pairing_sha256': sha(args.pairing),
             'stats': result['stats'], 'recursive_rounds': rounds, 'contradictions': rejected, 'sources': sources,
             'rejected_provisional_function_count': len(set(candidate['functions']) - set(functions)),
             'rejected_provisional_data_count': len(set(candidate['data']) - set(data)),
             'operational_contracts_with_unqualified_dependencies': [row['original'] for row, _ in pending_operand_contracts],
             'equivalent_complete_native_bodies': equivalent_bodies,
             'recursive_candidate_selection': 'all actual complete bodies; independent of provisional graph targets and tags',
             'policy': 'graph/string/order proposals do not become executable import identities without separate evidence',
             'device_access': False, 'collection_written': False}
    (args.out / 'receipt.json').write_text(json.dumps(audit, indent=2) + '\n')
    # Report real import coverage directly from the same retained dependency
    # closure. This inventory does not require a speculative arm_init identity
    # merely to count missing references; emit/placement gates stay unmet.
    roots = ['com.apple.kec.corecrypto', 'com.apple.kec.pthread', 'com.apple.filesystems.apfs',
             'com.apple.kec.Compression', 'com.apple.AppleFSCompression.AppleFSCompressionTypeZlib',
             'com.apple.iokit.IOGPUFamily', 'com.apple.iokit.IOHIDFamily', 'com.apple.iokit.IOStorageFamily']
    closure = dependency_closure(cache, roots)
    ranges = []
    missing = defaultdict(lambda: defaultdict(int))
    inside = lambda address: any(first <= address < last for first, last in original.mapped())
    qualified_addresses = {int(key, 16) for values in (functions, data) for key in values}
    for owner in closure:
        if owner not in cache.entries:
            continue
        image = Image(cache.data, cache.entries[owner][1])
        ranges.extend((first, last, owner) for first, last in image.mapped())
        for segment in image.segments:
            if segment['name'] != '__TEXT_EXEC' or not segment['filesize']:
                continue
            calls, _, _ = scan(image.read(segment['va'], segment['filesize']), segment['va'])
            for _, target, _ in calls:
                if inside(target) and target not in qualified_addresses:
                    missing[owner][hex(target)] += 1
    for site, pointer in cache.fixups().items():
        if inside(pointer[0]) and pointer[0] not in qualified_addresses:
            for first, last, owner in ranges:
                if first <= site < last:
                    missing[owner][hex(pointer[0])] += 1
                    break
    coverage = {'build': '22G100', 'arch': 'arm64e', 'qualification': 'INDEPENDENT_CONTRACTS_ONLY',
                'roots': roots, 'closure': sorted(closure), 'missing': missing,
                'missing_unique': len({key for rows in missing.values() for key in rows}),
                'mapping_complete': not bool(missing), 'layout_checked': False, 'boot_image_written': False,
                'page_geometry_bootstrap_identity_qualified': arm_init_qualified,
                'page_geometry_direct_store_qualified': direct_page_geometry,
                'page_geometry_qualified': arm_init_qualified or direct_page_geometry,
                'inventory_method': 'actual retained branch and chained-fixup targets; same default roots; no page-geometry/emit prerequisites bypassed'}
    (args.out / 'imports.json').write_text(json.dumps(coverage, indent=2) + '\n')
    audit.update(missing_unique=coverage['missing_unique'], mapping_complete=coverage['mapping_complete'],
                 page_geometry_bootstrap_identity_qualified=arm_init_qualified,
                 page_geometry_direct_store_qualified=direct_page_geometry,
                 page_geometry_qualified=arm_init_qualified or direct_page_geometry)
    (args.out / 'receipt.json').write_text(json.dumps(audit, indent=2) + '\n')
    print('Independent contracts:', len(functions), 'functions /', len(data), 'data; speculative graph proposals withheld')
    print('Actual retained imports still unqualified:', coverage['missing_unique'], '; no collection may be emitted')


if __name__ == '__main__':
    main()
