#!/usr/bin/env python3
"""Check native collection placement independently of unresolved import names.

Uses the pinned actual dependency closure and layout validator. Emits only
placement evidence; no relocated collection, rewritten branch or boot image.
An otherwise valid layout never implies the import map is complete.
"""
import argparse
from datetime import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('native-source', 'prelink-kit', 'stock-kernel', 'native-kernel', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    pin = tomllib.loads((ROOT / 'configs/zizhan-ios18e-inputs.toml').read_text())
    for path, expected in ((args.native_source, pin['source']['commit']), (args.prelink_kit, pin['prelink']['commit'])):
        if subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip() != expected:
            parser.error('Wrong pinned source/layout revision')
    if args.out.exists():
        parser.error('Retain previous placement evidence; choose a new output')
    sys.path[:0] = [str(args.prelink_kit.resolve()), str((args.native_source / 'scripts').resolve())]
    from kc_image import Image
    from fileset_emit import dependency_closure
    from pong_fileset_layout18e import install
    spec = importlib.util.spec_from_file_location('native_plan_roots', args.native_source / 'scripts/plan-pong-fileset18e.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    cache, native = Image(args.stock_kernel.read_bytes()), Image(args.native_kernel.read_bytes())
    closure = dependency_closure(cache, module.DEFAULT_ROOTS)
    report = {'build': '22G100', 'arch': 'arm64e', 'observed_at': datetime.now().astimezone().isoformat(),
              'stock_sha256': hashlib.sha256(args.stock_kernel.read_bytes()).hexdigest(),
              'native_sha256': hashlib.sha256(args.native_kernel.read_bytes()).hexdigest(),
              'roots': module.DEFAULT_ROOTS, 'closure': sorted(closure), 'bootable': False,
              'mapping_checked': False, 'collection_written': False, 'device_access': False}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    try:
        layout_class = install(args.prelink_kit.resolve())
        layout = layout_class(cache, native, closure)
        layout.validate()
        report.update(result='PLACEMENT_VERIFIED_IMPORTS_UNCHECKED',
                      placements={name: hex(value) for name, value in layout.placements.items()},
                      native_segments=[{'name': row['name'], 'bytes': row['size'], 'backed_bytes': row['filesize']} for row in native.segments])
    except BaseException as error:
        report.update(result='REJECTED', reason=str(error))
        raise
    finally:
        args.out.write_text(json.dumps(report, indent=2) + '\n')
    print('Native placement validated; import closure remains unchecked; no image emitted')


if __name__ == '__main__':
    main()
