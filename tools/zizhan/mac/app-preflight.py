#!/usr/bin/env python3
"""Read-only APP-01 prerequisites for an explicitly identified installed trial.

Do not infer the installed OS/UI from canonical prepared files. This checks
the pinned root/klog ranges, actual manifest and whole OS window against the
selected trial, and preserves the caller's expected UI/helper identities.
It never arms klog, boots a kernel or installs an image.
"""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

import install_pinned as install


def run(command, timeout=30):
    return subprocess.check_output(command, timeout=timeout).decode(errors='replace').strip()


def remote(command, timeout=30):
    return run([install.ADB, 'shell', '-T', 'su', '-c', command], timeout)


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(8 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trial', required=True)
    parser.add_argument('--backup', required=True, type=Path)
    parser.add_argument('--expected-ui', required=True)
    parser.add_argument('--expected-helper', required=True)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+', args.trial) or args.out.exists():
        parser.error('Use a safe trial name and fresh output')
    if not all(re.fullmatch(r'[0-9a-f]{64}', value)
               for value in (args.expected_ui, args.expected_helper)):
        parser.error('Require explicit SHA-256 identities from the reviewed candidate')
    root = Path(os.environ['ZIZHAN_SRC']).resolve()
    repo = Path(__file__).resolve().parents[3]
    trial = root / 'work/userland-22H374' / args.trial
    layout = json.loads((trial / 'ufs-layout.json').read_text())
    state = json.loads((trial / 'resume-install.json').read_text())
    klog = json.loads((root / 'klog.json').read_text())
    if state.get('manifest') is not True:
        raise RuntimeError('Selected trial is not recorded as installed')
    if (sha(trial / 'libpong-ui.dylib') != args.expected_ui or
        sha(trial / 'wifi-service18') != args.expected_helper):
        raise RuntimeError('Candidate UI/helper identity differs')
    os_window = next(window for window in layout['windows'] if window['name'] == 'OS')
    if sha(Path(os_window['image'])) != os_window['sha256']:
        raise RuntimeError('Candidate OS image differs')
    processes = run(['ps', '-axo', 'pid,command'])
    conflicts = [line for line in processes.splitlines() if any(marker in line for marker in
                 ('bash cycle.sh', '/cycle.sh ', '/cycle-resume.sh ',
                  '/when-back.sh ', 'install_pinned.py --', 'wifi-ui-next.py '))]
    if conflicts:
        raise RuntimeError('Device cycle/writer exists; reconcile its result first')
    if (run([install.ADB, 'get-state']) != 'device' or
        run([install.ADB, 'shell', 'getprop', 'ro.product.device']) != 'zizhan' or
        run([install.ADB, 'shell', 'getprop', 'sys.boot_completed']) != '1'):
        raise RuntimeError('Target Android completion is not verified')
    battery = run([install.ADB, 'shell', 'dumpsys', 'battery'])
    temperature = int(next(line.split(':')[1] for line in battery.splitlines()
                           if line.strip().startswith('temperature:')))
    # User requested unattended rounds without a cooling threshold. Retain
    # the actual reading for correlation, including sentinel/unknown values;
    # cycle deadline, identity, backup and return prerequisites still apply.
    records = [line.split('\t') for line in (args.backup / 'manifest.tsv').read_text().splitlines()]
    if not records or (args.backup / 'errors.txt').read_text().strip():
        raise RuntimeError('Backup inventory is absent or records errors')
    for name, _device, size, digest in records:
        if (Path(name).name != name or int(size) <= 0 or
            not re.fullmatch(r'[0-9a-f]{64}', digest) or
            not ((args.backup / (name + '.img.gz')).is_file() or (args.backup / name).is_file())):
            raise RuntimeError('Backup inventory is incomplete')
    for path, base, size in [('/data/hackios/root.img', state['base'], state['file_bytes']),
                             (klog['file'], klog['base'], klog['bytes'])]:
        if not re.fullmatch(r'/data/hackios/[A-Za-z0-9_.-]+', path):
            raise RuntimeError('Unexpected pinned path')
        mapping = remote(f'{install.PIN} map {path}')
        extents = [tuple(map(int, line.split()[1:5])) for line in mapping.splitlines()
                   if line.startswith('EXT ')]
        if not extents or install.contiguous(extents, size) != base:
            raise RuntimeError('Pinned file geometry changed')
    if not (klog['base'] + klog['bytes'] <= state['base'] or
            state['base'] + state['file_bytes'] <= klog['base']):
        raise RuntimeError('Pinned root/klog ranges overlap')
    expected = install.manifest(layout['windows'], state['base'], state['file_bytes'])
    live = subprocess.check_output([install.ADB, 'exec-out',
            f"su -c 'dd if={install.DEV} bs=4096 skip={state['base'] // 4096} count=1 2>/dev/null'"], timeout=30)
    if live != expected:
        raise RuntimeError('Installed manifest differs from selected trial')
    live_sha = remote(f"dd if={install.DEV} bs=4096 "
                      f"skip={(state['base'] + os_window['offset']) // 4096} "
                      f"count={os_window['size'] // 4096} 2>/dev/null | sha256sum", 240).split()[0]
    if live_sha != os_window['sha256']:
        raise RuntimeError('Whole installed OS differs from selected candidate')
    subprocess.run(['bash', str(repo / 'tools/zizhan/mac/usbkick.sh'), 'remember'],
                   check=True, timeout=30, stdout=subprocess.DEVNULL)
    result = {'result': 'PASS', 'observed_at': datetime.now().astimezone().isoformat(),
              'source_revision': run(['git', '-C', str(repo), 'rev-parse', 'HEAD']),
              'dirty': run(['git', '-C', str(repo), 'status', '--short']).splitlines(),
              'operational_os': args.trial, 'temperature_tenths_c': temperature,
              'backup_manifest_entries': len(records),
              'backup_scope': 'presence and recorded errors; not a fresh complete backup hash audit',
              'root_base': state['base'], 'root_bytes': state['file_bytes'], 'klog': klog,
              'manifest_sha256': hashlib.sha256(expected).hexdigest(),
              'os_sha256': live_sha, 'live_os_verified': True,
              'ui_sha256': args.expected_ui, 'helper_sha256': args.expected_helper,
              'device_writes': False}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS explicit installed trial, target, backup, ranges, manifest and whole OS')


if __name__ == '__main__':
    main()
