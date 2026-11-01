#!/usr/bin/env python3
"""Mac: the pinned klog file that the kernel copies the ramoops zones into (NativeUfs pong_klog_flush, boot-arg pong_klog_4k).

The ramoops console survives the reset in DRAM, but once the phone is warm the self-refresh flips bits in it (g54-g104 lost
most FPS summaries). Instead, before the deadline/panic reset the kernel writes both zones (CONSOLE, DIAG: 4 MiB, raw) with
FUA writes into the blocks behind a header block of a pinned Android file, which the host reads back raw from userdata
(below f2fs and dm-default-key, like install_pinned.py's root image).

usage: klog.py setup            create /data/hackios/klog.pin (pinned, contiguous) and save its block to klog.json
       klog.py arg              print the boot-arg (pong_klog_4k=<4 KiB block relative to userdata>), nothing if no setup
       klog.py arm              header in "armed" state + zeroed data, so a stale copy is never taken for this round's
       klog.py read OUT         if the kernel marked the header written, save the zones as RamoopsDump's XNULOG text to OUT
Header block: "PONGKLOG", le64 own 4 KiB block, le32 data blocks (1024), le32 state (0 armed, 1 written, 2 write failed),
le64 flushes, le64 mach time, le32 reason (1 deadline, 2 panic), le32 IOReturn.
"""
import json, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from install_pinned import su, extents, contiguous, PIN, DEV, SECTION, ADB
import subprocess

FILE = '/data/hackios/klog.pin'
STATE = Path(__file__).resolve().parent / 'klog.json'
DATA_BLOCKS = 1024                      # both 2 MiB ramoops zones
FILE_BYTES = (4096 * (1 + DATA_BLOCKS) + SECTION - 1) // SECTION * SECTION
ZONE = 0x200000
RAMOOPS = 0x85200000
SIG = 0x43474244


def header(block4k, state=0):
    h = bytearray(4096)
    h[:8] = b'PONGKLOG'
    struct.pack_into('<QII', h, 8, block4k, DATA_BLOCKS, state)
    return bytes(h)


def load():
    return json.loads(STATE.read_text()) if STATE.exists() else None


def setup():
    su('mkdir -p /data/hackios && chmod 700 /data/hackios')
    if not su(f'ls {FILE} 2>/dev/null', check=False).strip():
        out = su(f'{PIN} create {FILE} {FILE_BYTES}')
        if out.startswith('ERR'):
            raise SystemExit(out)
    base = contiguous(extents(FILE), FILE_BYTES)
    if base is None or base % 4096:
        raise SystemExit(f'{FILE} is not one contiguous, block-aligned run; delete it and retry')
    root = json.loads((Path(__file__).resolve().parent / 'install-pinned.json').read_text()) \
        if (Path(__file__).resolve().parent / 'install-pinned.json').exists() else None
    if root and base < root['base'] + root['file_bytes'] and root['base'] < base + FILE_BYTES:
        raise SystemExit(f'{FILE} at {base:#x} overlaps the root image')
    STATE.write_text(json.dumps({'file': FILE, 'base': base, 'bytes': FILE_BYTES}) + '\n')
    print(f'{FILE}: base {base:#x}; boot-arg pong_klog_4k={base // 4096}')


def arm():
    st = load()
    if not st:
        return
    base = st['base']
    assert base + 4096 * (1 + DATA_BLOCKS) <= base + st['bytes']
    # Nothing outside the file is touched: one header block, then exactly DATA_BLOCKS zero blocks.
    su(f'dd if=/dev/zero of={DEV} bs=4096 seek={base // 4096 + 1} count={DATA_BLOCKS} conv=notrunc 2>/dev/null')
    su(f'dd of={DEV} bs=4096 seek={base // 4096} count=1 conv=notrunc 2>/dev/null; sync', header(base // 4096))
    got = raw(base, 1)
    if got[:24] != header(base // 4096)[:24]:
        raise SystemExit('klog header readback mismatch')


def raw(base, blocks):
    r = subprocess.run([ADB, 'exec-out', f"su -c 'dd if={DEV} bs=4096 skip={base // 4096} count={blocks} 2>/dev/null'"],
                       capture_output=True, check=True)
    return r.stdout


def zone(name, data, base):
    sig, start, size = struct.unpack_from('<III', data)
    head = f'\n===== {name} @{base:x} sig={sig:08x} start={start:x} size={size:x} =====\n'.encode()
    cap = ZONE - 12
    if sig != SIG or size > cap or start > cap:
        return head
    body = data[12:12 + cap]
    return head + (body[:size] if size < cap else body[start:] + body[:start])


def read(out):
    st = load()
    if not st:
        raise SystemExit('no klog.json: run klog.py setup')
    blob = raw(st['base'], 1 + DATA_BLOCKS)
    h = blob[:4096]
    if h[:8] != b'PONGKLOG' or struct.unpack_from('<Q', h, 8)[0] != st['base'] // 4096:
        raise SystemExit('klog header missing')
    state, flushes, when, reason, status = struct.unpack_from('<IQQII', h, 20)
    if state != 1:
        raise SystemExit(f'klog not written this round (state {state}, flushes {flushes}, reason {reason}, status {status:#x})')
    data = blob[4096:]
    text = b'XNULOG v1 (zizhan klog: reason %d, flush %d)\n' % (reason, flushes)
    text += zone('CONSOLE', data[:ZONE], RAMOOPS) + zone('DIAG', data[ZONE:2 * ZONE], RAMOOPS + ZONE)
    Path(out).write_bytes(text)
    name = {1: 'deadline', 2: 'panic', 3: 'flip-mark', 4: 'power-key', 5: 'periodic'}.get(reason, reason)
    print(f'klog: {len(text)} bytes, reason {name}')


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'setup':
        setup()
    elif cmd == 'arg':
        st = load()
        if st:
            print(f'pong_klog_4k={st["base"] // 4096}')
    elif cmd == 'arm':
        arm()
    elif cmd == 'read' and len(sys.argv) == 3:
        read(sys.argv[2])
    else:
        raise SystemExit(__doc__)
