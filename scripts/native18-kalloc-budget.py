#!/usr/bin/env python3
"""Reproduce typed-zone policy from actual kernel metadata before another boot.

Parse the real fixed kalloc views, preserve exact signature/name ordering and
prefix grouping, and compile the selected XNU's actual allocation policy with
ASan/UBSan. This does not remove its 32-slot assertion, create kernel zones,
modify the kernel, or operate the phone. Generated Apple source stays private.
"""
import argparse
import bisect
from collections import Counter
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
    for key in ('prelink-kit', 'kernel', 'xnu-source', 'out'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    public = Path(__file__).resolve().parents[1]
    if args.out.exists() or public == args.out.resolve() or public in args.out.resolve().parents:
        parser.error('Use a fresh private output; Apple source and generated evidence stay local')
    sys.path.insert(0, str(args.prelink_kit.resolve()))
    from kc_image import Image
    image = Image(args.kernel.read_bytes())
    fixups = image.fixups()
    symbols = {name: address for address, names in image.symbols().items() for name in names}
    source_path = args.xnu_source / 'osfmk/kern/kalloc.c'
    source = source_path.read_text()
    config = symbols['_kt_zone_cfg']
    sizes = list(struct.unpack('<29H', image.read(config, 58)))
    if sizes != sorted(set(sizes)) or sizes[-1] != 32768 or sizes[0] != 16:
        parser.error('Review actual size-class configuration')
    section = next(row for row in image.sections if row['name'] == '__kalloc_type')
    if section['size'] % 64:
        parser.error('Unexpected actual kalloc_type_view extent')
    records, skipped, flags = [], Counter(), Counter()
    for address in range(section['va'], section['va'] + section['size'], 64):
        flag, size = struct.unpack('<II', image.read(address + 40, 8))
        flags[hex(flag)] += 1
        if flag & 0x60 != 0x60:
            parser.error('Actual view does not carry the required version flags')
        if flag & 0x10:
            skipped['VM'] += 1
            continue
        if flag & 8:
            skipped['data-only'] += 1
            continue
        name = image.cstring(fixups[address + 16][0])
        signature = image.cstring(fixups[address + 32][0])
        if name is None or signature is None or size > sizes[-1]:
            parser.error('Actual typed view has an unsupported name/signature/size')
        records.append((bisect.bisect_left(sizes, size), signature, name, size))
    records.sort()
    unique, total = [0] * len(sizes), [0] * len(sizes)
    previous = None
    for index, signature, name, _ in records:
        if index != previous:
            unique[index] = total[index] = 1
            previous, prior_signature, prior_name = index, signature, name
        elif not signature.startswith(prior_signature):
            unique[index] += 1
            total[index] += 1
            prior_signature, prior_name = signature, name
        else:
            prior_signature = signature
            if name != prior_name:
                total[index] += 1
                prior_name = name
    duplicates = [count - sig for count, sig in zip(total, unique)]
    first = source.rfind('__startup_func', 0, source.index('kalloc_type_distribute_budget('))
    last = source.rfind('__startup_func', 0, source.index('kalloc_type_create_zone_for_size('))
    policy = source[first:last]
    required = ('kalloc_type_distribute_budget(', 'kalloc_type_cmp_type_zones(',
                'kalloc_type_redistribute_budget(', 'kalloc_type_apply_policy(')
    if first < 0 or last <= first or any(value not in policy for value in required):
        parser.error('Review changed actual typed-zone policy boundaries')
    args.out.mkdir(parents=True)
    prefix = ('#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <stdlib.h>\n'
              '#include <assert.h>\n#define __startup_func\n#define DEBUG 0\n#define DEVELOPMENT 0\n'
              '#define MAX_K_ZONE(x) (sizeof(x)/sizeof((x)[0]))\n#define KT_OPTIONS_DEBUG 2\n'
              'static unsigned kt_options=0;\nstruct nzones_with_idx {uint16_t nzones; uint16_t idx;};\n')
    prefix += 'static const uint16_t kt_zone_cfg[]={' + ','.join(map(str, sizes)) + '};\n'
    suffix = ('\nint main(void){\n'
              'uint16_t f[]={' + ','.join(map(str, unique)) + '};\n'
              'uint16_t t[]={' + ','.join(map(str, duplicates)) + '};\n'
              'for(unsigned budget=80;budget<=300;budget++){\n'
              'uint16_t a[29]={0},b[29]={0};\n'
              '(void)kalloc_type_apply_policy(f,t,a,b,(uint16_t)budget);\n'
              'unsigned max=0,idx=0;for(unsigned i=0;i<29;i++){if((unsigned)a[i]+b[i]+1>max){max=a[i]+b[i]+1;idx=i;}}\n'
              'printf("%u,%u,%u,%u,%u\\n",budget,max,kt_zone_cfg[idx],a[idx],b[idx]);}\nreturn 0;}\n')
    cfile, binary = args.out / 'actual-policy.c', args.out / 'actual-policy'
    cfile.write_text(prefix + policy + suffix)
    command = ['clang', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
               '-fno-sanitize-recover=all', str(cfile), '-o', str(binary)]
    compile_result = subprocess.run(command, capture_output=True, text=True)
    (args.out / 'compile.log').write_text(compile_result.stdout + compile_result.stderr)
    if compile_result.returncode:
        raise RuntimeError('Actual typed-zone policy host compilation failed')
    run = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
    (args.out / 'budget-results.csv').write_text('budget,max_slots,size_class,signature_zones,type_zones\n' + run.stdout)
    rows = [dict(zip(('budget', 'max_slots', 'size_class', 'signature_zones', 'type_zones'), map(int, line.split(','))))
            for line in run.stdout.splitlines()]
    configured, = struct.unpack('<H', image.read(symbols['_kt_fixed_zones'], 2))
    current = next(row for row in rows if row['budget'] == configured)
    safe = [row for row in rows if row['max_slots'] <= 32]
    report = {'observed_at': datetime.now().astimezone().isoformat(), 'result': 'ACTUAL_TYPED_ZONE_POLICY_REPLAYED',
              'kernel_sha256': sha(args.kernel), 'source_policy_sha256': hashlib.sha256(policy.encode()).hexdigest(),
              'view_count': section['size'] // 64, 'included_fixed_views': len(records), 'skipped': skipped,
              'flags': flags, 'size_classes': sizes, 'unique_signatures': unique, 'extra_types': duplicates,
              'actual_default': current, 'safe_budgets_tested': [row['budget'] for row in safe],
              'supported_tunable': 'kt_fixed_zones', 'sample_120': next(row for row in rows if row['budget'] == 120),
              'assertion_retained': True, 'device_access': False, 'kernel_modified': False,
              'scope': 'source-only kernel actual metadata distribution; fileset adds real component views and needs fresh inventory',
              'host_command': command}
    (args.out / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('result', 'included_fixed_views', 'actual_default',
                                                 'sample_120')}), flush=True)


if __name__ == '__main__':
    main()
