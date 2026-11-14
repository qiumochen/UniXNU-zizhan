#!/usr/bin/env python3
"""Build isolated native Metal components with zizhan's maintained wire ABI.

Snapshots actual dependency files, including recorded local adaptations. Never
modifies dependency checkouts, prior build trees, mounted images or the phone.
"""
import argparse
from datetime import datetime
import difflib
import hashlib
import json
import re
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot(source, destination):
    source = source.resolve()
    names = subprocess.check_output(['git', '-C', str(source), 'ls-files', '-z', '--cached', '--others', '--exclude-standard']).split(b'\0')
    rows = []
    for name in sorted(set(names)):
        if not name:
            continue
        relative = Path(os.fsdecode(name))
        path, target = source / relative, destination / relative
        if not path.exists() and not path.is_symlink():  # a tracked deletion is part of the dirty state
            continue
        if path.is_symlink():
            link = os.readlink(path)
            if Path(link).is_absolute() or not path.resolve().is_relative_to(source):
                raise ValueError('Dependency link escapes source: ' + str(path))
            target.parent.mkdir(parents=True, exist_ok=True)
            target.symlink_to(link)
            rows.append({'path': str(relative), 'symlink': link})
            continue
        if not path.is_file():
            raise ValueError('Review non-regular dependency input: ' + str(path))
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        rows.append({'path': str(relative), 'sha256': sha(target), 'bytes': target.stat().st_size})
    return {'source': str(source),
            'commit': subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip(),
            'status': subprocess.check_output(['git', '-C', str(source), 'status', '--short'], text=True),
            'files': rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('turnip', 'bridge', 'plugin'))
    for name in ('native-source', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('mesa', 'translator', 'reference-build', 'layout', 'pac-rewriter', 'bridge-directory', 'turnip-library',
                 'runtime-patches', 'core-reference'):
        parser.add_argument('--' + name, type=Path)
    parser.add_argument('--llvm-bin', type=Path, default=Path('/opt/homebrew/opt/llvm@18/bin'))
    parser.add_argument('--bison', type=Path, default=Path('/opt/homebrew/opt/bison/bin/bison'))
    args = parser.parse_args()
    profile = tomllib.loads((ROOT / 'configs/zizhan-ios18e-inputs.toml').read_text())
    source, out = args.native_source.resolve(), args.out.resolve()
    if subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() != profile['source']['commit']:
        parser.error('Wrong native source revision')
    if out.exists() or out == ROOT or ROOT in out.parents:
        parser.error('Use a new machine-local output directory')
    required = {'turnip': ('mesa', 'reference_build'),
                'bridge': ('translator', 'mesa', 'layout', 'pac_rewriter'),
                'plugin': ('bridge_directory', 'turnip_library')}[args.stage]
    if any(getattr(args, name) is None for name in required):
        parser.error('Stage requires: ' + ', '.join(required))
    out.mkdir(parents=True)
    overlay = out / 'source-overlay'
    scripts = overlay / 'scripts'
    scripts.mkdir(parents=True)
    for name in ('pong_native_inputs.py', 'pong_paths.py', 'build-pong-turnip18e.py',
                 'build-pong-metal18e.py', 'build-pong-metal-plugin.py'):
        shutil.copy2(source / 'scripts' / name, scripts / name)
    report = {'stage': args.stage, 'build': '22G100', 'arch': 'arm64e',
              'started_at': datetime.now().astimezone().isoformat(), 'source': profile['source'],
              'device_source_head': subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
              'device_source_dirty': subprocess.check_output(['git', '-C', str(ROOT), 'status', '--short'], text=True),
              'runtime_qualified': False, 'device_access': False, 'dependencies': []}
    record = out / 'receipt.json'
    env = dict(os.environ, CARGO_BUILD_JOBS='6', RUSTC_BOOTSTRAP='1')
    env['PATH'] = os.pathsep.join([str(Path.home() / '.cargo/bin'), '/opt/homebrew/bin', env['PATH']])
    if args.stage == 'turnip':
        report['dependencies'].append(snapshot(args.mesa, out / 'mesa'))
        shutil.copytree(ROOT / 'drivers/PongAdreno/submit', overlay / 'drivers/PongAdreno/submit')
        builder = scripts / 'build-pong-turnip18e.py'
        text, replaced = re.subn(r"Path\('[^']*/bison'\)",
                                 lambda _match: 'Path(' + repr(str(args.bison.resolve())) + ')',
                                 builder.read_text())
        if replaced != 1:
            raise RuntimeError('Expected one pinned upstream bison path to replace')
        text = text.replace("'/opt/local/bin/ninja'", repr(shutil.which('ninja', path=env['PATH'])))
        builder.write_text(text)
        command = [sys.executable, str(builder), '--mesa', str(out / 'mesa'),
                   '--reference-build', str(args.reference_build.resolve()), '--out', str(out / 'build')]
        artifacts = [out / 'build/src/freedreno/vulkan/libvulkan_freedreno.dylib']
    elif args.stage == 'bridge':
        layout = json.loads(args.layout.read_text())
        if layout.get('system') != 'iOS18.6.2/22G100' or layout['device']['instance_size'] != 600:
            parser.error('Require freshly described actual native Metal metadata')
        report['dependencies'].append(snapshot(args.translator, out / 'translator'))
        if bool(args.runtime_patches) != bool(args.core_reference):
            parser.error('Runtime patches and their exact core reference must be paired')
        if args.runtime_patches:
            pins = profile['metal_runtime']
            if report['dependencies'][-1]['commit'] != pins['translator'] or report['dependencies'][-1]['status']:
                parser.error('Runtime patch series requires its clean, exact translator base')
            for path, key in ((args.runtime_patches, 'patch_source'), (args.core_reference, 'core')):
                if json.loads((path / 'source-receipt.json').read_text())['commit'] != pins[key]:
                    parser.error('Runtime patch source differs from native pins')
            recipe = (args.core_reference / 'scripts/build-adreno-metal-bridge.py').read_text()
            first = recipe.index('PATCHES = [')
            names = re.findall(r"'(\d{4}-[^']+\.patch)'", recipe[first:recipe.index(']]', first)])
            if len(names) != 30:
                parser.error('Review changed upstream runtime patch series')
            report['runtime_patches'] = []
            for name in names:
                patch = args.runtime_patches / 'bridge/patches' / name
                subprocess.run(['git', 'apply', '--whitespace=nowarn', str(patch.resolve())], cwd=out / 'translator', check=True)
                report['runtime_patches'].append({'name': name, 'sha256': sha(patch)})
            # Pong's bounded summary predates upstream's stage filtering.
            # Preserve the filter and insert exactly the original added code,
            # recording the generated diff rather than applying with fuzz.
            summary = source / 'patches/metal2vulkan/0011-pong-bounded-command-stage-summary.patch'
            groups = []
            for part in summary.read_text().split('@@ ')[1:]:
                if not part.startswith('-'):
                    continue
                additions = ''.join(line[1:] + '\n' for line in part.splitlines()[1:] if line.startswith('+') and not line.startswith('+++'))
                groups.append(additions)
            if len(groups) != 3:
                raise ValueError('Review changed native bounded-summary patch')
            diffs = []
            for relative, action in (
                ('crates/metal2vulkan-runtime/src/stage_trace.rs', 'trace'),
                ('crates/metal2vulkan-runtime/src/command_buffer/mod.rs', 'command')):
                path = out / 'translator' / relative
                before = path.read_text()
                if action == 'trace':
                    anchor = 'static NEXT: AtomicU64 = AtomicU64::new(1);\n'
                    condition = '    if !diagnostics() {\n'
                    if before.count(anchor) != 1 or before.count(condition) != 1:
                        raise ValueError('Review changed stage ring/diagnostic filter')
                    after = before.replace(anchor, anchor + groups[0]).replace(condition, groups[1])
                else:
                    anchor = '    pub fn commit(&self) -> Result<(), Error> {\n'
                    if before.count(anchor) != 1:
                        raise ValueError('Review changed actual command commit method')
                    after = before.replace(anchor, groups[2] + anchor)
                path.write_text(after)
                diffs.extend(difflib.unified_diff(before.splitlines(True), after.splitlines(True), 'a/' + relative, 'b/' + relative))
            (out / 'native-bounded-summary.patch').write_text(''.join(diffs))
            report['bounded_summary'] = {'source_sha256': sha(summary), 'adapted_patch_sha256': sha(out / 'native-bounded-summary.patch'),
                                         'retained': 'upstream stage filtering and bounded native ring'}
        shutil.copytree(source / 'drivers/PongMetal', overlay / 'drivers/PongMetal', ignore=shutil.ignore_patterns('.git', '__pycache__'))
        # Native object/frontend code is retained. The plugin is built in its
        # own stage with the maintained kernel/presenter wire protocol.
        pac = out / 'pac-kit/scripts'
        pac.mkdir(parents=True)
        shutil.copy2(args.pac_rewriter, pac / 'rust-pac-rewrite.py')
        report['pac_rewriter_sha256'] = sha(args.pac_rewriter)
        report['layout_sha256'] = sha(args.layout)
        builder = scripts / 'build-pong-metal18e.py'
        text = builder.read_text()
        for name in ('llvm-as', 'llvm-objcopy'):
            tool = args.llvm_bin / name
            if not tool.is_file():
                parser.error('Missing LLVM tool ' + str(tool))
            text = text.replace('/opt/local/bin/' + name + '-mp-18', str(tool.resolve()))
        builder.write_text(text)
        command = [sys.executable, str(builder), '--translator', str(out / 'translator'),
                   '--mesa', str(args.mesa.resolve()), '--unixnu', str(out / 'pac-kit'),
                   '--layout', str(args.layout.resolve()), '--out', str(out / 'build')]
        artifacts = [out / 'build/PongMetalBridge.dylib']
    else:
        library = args.turnip_library.resolve()
        # A native discovery bundle cannot rescue an arm64 Turnip binary.
        header = library.read_bytes()[:12]
        if header[:4] != b'\xcf\xfa\xed\xfe' or int.from_bytes(header[8:12], 'little') & 0xffffff != 2:
            parser.error('Require a real arm64e Turnip library')
        target = overlay / 'drivers/PongMetal'
        shutil.copytree(ROOT / 'drivers/PongMetal', target)
        shutil.copytree(ROOT / 'drivers/PongAdreno/submit', overlay / 'drivers/PongAdreno/submit')
        shutil.copy2(ROOT / 'drivers/PongAdreno/PongScanoutWire.h', overlay / 'drivers/PongAdreno/PongScanoutWire.h')
        # Select native OS property/metadata policy without importing the
        # upstream scanout generation/idle/opaque-copy wire changes.
        for path in target.rglob('*'):
            if path.suffix in ('.mm', '.h'):
                path.write_text(path.read_text().replace('22H374', '22G100'))
        command = [sys.executable, str(scripts / 'build-pong-metal-plugin.py'),
                   '--bridge-directory', str(args.bridge_directory.resolve()),
                   '--guest-build', '22G100', '--turnip-library', str(library),
                   '--output-dir', str(out / 'build'), '--clients', '*']
        artifacts = [out / 'build/PongAdrenoMetal.bundle' / name for name in
                     ('PongAdrenoMetal', 'PongMetalBridge.dylib', 'libvulkan_freedreno.dylib')]
        report['presenter_contract'] = 'maintained zizhan token/evict/target protocol; no native upstream idle/generation substitution'
    report['command'] = command
    report['generated_sources'] = [{'path': str(path.relative_to(out)), 'sha256': sha(path)}
                                   for path in overlay.rglob('*') if path.is_file()]
    record.write_text(json.dumps(report, indent=2) + '\n')
    with (out / 'build.log').open('w') as log:
        result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
    report.update(returncode=result.returncode, finished_at=datetime.now().astimezone().isoformat())
    if result.returncode == 0:
        for path in artifacts:
            subprocess.run(['codesign', '--force', '--sign', '-', '--pagesize', '4096', str(path)], check=True)
            subprocess.run(['codesign', '--verify', '--strict', str(path)], check=True)
        report['artifacts'] = [{'path': str(path), 'sha256': sha(path), 'bytes': path.stat().st_size} for path in artifacts]
    record.write_text(json.dumps(report, indent=2) + '\n')
    print(args.stage, 'returncode', result.returncode, '; host build only', flush=True)
    raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
