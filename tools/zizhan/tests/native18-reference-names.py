#!/usr/bin/env python3
"""Check actual reference-name qualification and UPL naming regression.

Uses the real closure selector. Optional local reference files verify the
observed three-argument vm_upl_map versus two-argument ubc_upl_map conflict.
No kernel, image or device execution.
"""
import argparse
import importlib.util
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', action='append', type=Path, default=[])
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Retain prior controls; choose a new output')
    root = Path(__file__).resolve().parents[3]
    path = root / 'scripts/native18-closure.py'
    spec = importlib.util.spec_from_file_location('actual_native_closure_selector', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    exact = 'Unique bidirectional exact relocated-instruction body after entry/return PAC framing only; internal branches and available strings agree'
    weak = 'Same-Darwin native naming reference through upstream calls checks; reference addresses are not emitted'
    def reference(name, evidence):
        return {'build': '22G100', 'reference_role': 'names-only', 'functions':
                [{'original': '0x1000', 'names': [name], 'instructions': 32, 'evidence': evidence}]}
    symbols = {'_ubc_upl_map': 0x2000, '_vm_upl_map': 0x3000}
    rows = []
    for order in (False, True):
        values = [('weak', reference('_ubc_upl_map', weak)), ('exact', reference('_vm_upl_map', exact))]
        if order:
            values.reverse()
        selected, rejected = module.select_reference_identities(values, symbols)
        assert len(selected) == 1 and selected[0]['names'] == ['_vm_upl_map'] and len(rejected) == 1
        rows.append({'name': 'exact-over-weak-order-' + str(order), 'result': 'PASS'})
    selected, _ = module.select_reference_identities([('a', reference('_vm_upl_map', exact)), ('b', reference('_ubc_upl_map', exact))], symbols)
    assert not selected
    rows.append({'name': 'contradicting-exact-code-identities-refuse', 'result': 'PASS'})
    selected, _ = module.select_reference_identities([('only-graph', reference('_ubc_upl_map', weak))], symbols)
    assert not selected
    rows.append({'name': 'graph-only-reference-does-not-become-reviewed', 'result': 'PASS'})
    selected, _ = module.select_reference_identities([('missing-symbol', reference('_missing', exact))], symbols)
    assert not selected
    rows.append({'name': 'unavailable-exact-source-symbol-refuses', 'result': 'PASS'})
    operand = reference('_vm_upl_map', 'Unique bidirectional complete instruction body with normalized compiler PAC checks; every operand closed')
    operand['functions'][0]['operand_proof'] = 'complete-v1'
    selected, _ = module.select_reference_identities([('complete-operands', operand)], symbols)
    assert len(selected) == 1
    rows.append({'name': 'complete-operand-reference-selected', 'result': 'PASS'})
    for marker in ({'review_only': True}, {'binding_authorized': False}):
        refused = json.loads(json.dumps(operand))
        refused['functions'][0].update(marker)
        selected, _ = module.select_reference_identities([('explicitly-withheld', refused)], symbols)
        assert not selected
        rows.append({'name': 'explicitly-withheld-' + next(iter(marker)), 'result': 'PASS'})
    if args.reference:
        references = [(str(path), json.loads(path.read_text())) for path in args.reference]
        selected, rejected = module.select_reference_identities(references, symbols)
        actual = [row for row in selected if row['original'] == '0xfffffff0082803e8']
        assert len(actual) == 1 and actual[0]['names'] == ['_vm_upl_map']
        rows.append({'name': 'actual-four-reference-UPL-arity-regression', 'result': 'PASS',
                     'source_names': actual[0]['names'], 'supporting_exact_references': len(actual[0]['supporting_exact_references'])})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'result': 'PASS', 'controls': rows, 'device_access': False}, indent=2) + '\n')
    print('PASS actual native reference selector:', len(rows), 'controls')


if __name__ == '__main__':
    main()
