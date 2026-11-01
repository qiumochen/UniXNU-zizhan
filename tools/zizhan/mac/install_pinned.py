#!/usr/bin/env python3
"""Install the SHNGROOT root image into one contiguous f2fs pinned file on a live Android /data.

zizhan keeps Android: instead of overwriting userdata from block 0 (Pong), the OS / SystemOS / Data
windows are written raw (below f2fs and dm-default-key) into the physical blocks of a pinned,
fallocated file. The SHNGROOT manifest goes into the file's first 4 KiB block; boot XNU with
`pong_shngroot_4k=<printed value>`. Window offsets in the manifest are relative to the userdata
partition, so the patched NativeUfs driver needs no extent tables.

usage: install_pinned.py --layout ufs-layout.json [--file /data/hackios/root.img] [--resume] [--map-only] [--only OS]
Runs on the Mac with the phone in Android (adb root via KernelSU `su`).
"""
import argparse, gzip, hashlib, json, os, struct, subprocess, sys, time
from pathlib import Path

MIB = 1 << 20
SECTION = 2 * MIB                 # f2fs section; pinned allocation is section granular
CHUNK = 64 * MIB
DEV = '/dev/block/by-name/userdata'
PIN = '/data/local/tmp/pinalloc'
ADB = os.environ.get('ADB', '/opt/homebrew/bin/adb')


def su(cmd, data=None, check=True):
    r = subprocess.run([ADB, 'shell', '-T', f"su -c '{cmd}'"], input=data, capture_output=True)
    out = r.stdout.decode(errors='replace')
    if check and r.returncode:
        raise SystemExit(f'device command failed ({r.returncode}): {cmd}\n{out}{r.stderr.decode(errors="replace")}')
    return out


def extents(path):
    out = su(f'{PIN} map {path}')
    ext = [tuple(map(int, l.split()[1:5])) for l in out.splitlines() if l.startswith('EXT ')]
    if not ext:
        raise SystemExit(f'no extents for {path}:\n{out}')
    return ext


def contiguous(ext, size):
    """Return the physical start if the file is one physically contiguous run covering size."""
    pos, phys0 = 0, ext[0][1]
    for logical, physical, length, flags in ext:
        if logical != pos or physical != phys0 + logical:
            return None
        pos += length
    return phys0 if pos >= size else None


def manifest(windows, base, file_bytes):
    h = bytearray(4096)
    h[:8] = b'SHNGROOT'
    struct.pack_into('<IIQ', h, 8, 1, len(windows), base + file_bytes)
    for i, w in enumerate(windows):
        struct.pack_into('<32sQQII32s40s', h, 24 + i * 128, w['name'].encode(), base + w['offset'], w['size'],
                         512, 1, bytes.fromhex(w['sha256']), bytes(40))
    h[4064:] = hashlib.sha256(h[:4064]).digest()
    return bytes(h)


BOUNDS = None   # (first, end) byte range of the pinned file; nothing outside it is ever written


def write_region(phys, data, label):
    """Write bytes at a userdata byte offset (1 MiB aligned) and verify by reading back."""
    assert phys % MIB == 0 and len(data) % 4096 == 0
    assert BOUNDS and BOUNDS[0] <= phys and phys + len(data) <= BOUNDS[1], (label, phys, BOUNDS)
    want = hashlib.sha256(data).hexdigest()
    payload = gzip.compress(data, compresslevel=1)
    su(f'gzip -dc | dd of={DEV} bs=1048576 seek={phys // MIB} 2>/dev/null', payload)
    got = su(f'sync; dd if={DEV} bs=4096 skip={phys // 4096} count={len(data) // 4096} 2>/dev/null | sha256sum').split()[0]
    if got != want:
        raise SystemExit(f'{label}: readback mismatch at {phys:#x} want {want} got {got}')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--layout', type=Path, required=True, help='ufs-layout.json from prepare-ipados18-ufs-layout.py')
    p.add_argument('--file', default='/data/hackios/root.img')
    p.add_argument('--spacer', default='/data/hackios-test/spacer.pin',
                   help='pinned filler allocated first so the root file lands in untouched, contiguous space')
    p.add_argument('--spacer-gib', type=int, default=40)
    p.add_argument('--state', type=Path, default=Path('install-pinned.json'))
    p.add_argument('--resume', action='store_true')
    p.add_argument('--map-only', action='store_true', help='allocate and map the file, write nothing')
    p.add_argument('--only', action='append', default=[],
                   help='write only these windows (e.g. --only OS after refresh_metal); the manifest is always rewritten')
    a = p.parse_args()

    layout = json.loads(a.layout.read_text())
    windows = layout['windows']
    file_bytes = (layout['image_bytes'] + SECTION - 1) // SECTION * SECTION
    state = json.loads(a.state.read_text()) if a.resume and a.state.exists() else {'done': {}}

    d = os.path.dirname(a.file)
    su(f'mkdir -p {d} && chmod 700 {d}')
    if su(f'ls {a.file} 2>/dev/null', check=False).strip():
        ext = extents(a.file)
    else:
        if not su(f'ls {a.spacer} 2>/dev/null', check=False).strip():
            print(su(f'{PIN} create {a.spacer} {a.spacer_gib << 30} | head -1'))
        out = su(f'{PIN} create {a.file} {file_bytes}')
        if out.startswith('ERR'):
            raise SystemExit(out)
        ext = extents(a.file)
    base = contiguous(ext, file_bytes)
    print(f'{a.file}: {len(ext)} extent(s), {file_bytes} bytes, base={base}')
    if base is None:
        raise SystemExit('root file is not one contiguous run; delete it, grow the spacer and retry')
    if base % MIB:
        raise SystemExit(f'base {base:#x} is not 1 MiB aligned')
    if state.get('base') not in (None, base):
        raise SystemExit(f'file moved since the last run ({state["base"]:#x} -> {base:#x}); refusing to resume')
    state['base'], state['file_bytes'] = base, file_bytes
    global BOUNDS
    BOUNDS = (base, base + file_bytes)
    if any(w['offset'] < 4096 or w['offset'] + w['size'] > file_bytes for w in windows):
        raise SystemExit('a window falls outside the pinned file')
    print(f'boot-arg: pong_shngroot_4k={base // 4096}')
    if a.map_only:
        return

    t0 = time.time()
    for w in windows:
        if a.only and w['name'] not in a.only:
            continue
        size, img = w['size'], Path(w['image'])
        with img.open('rb') as f:
            for off in range(0, size, CHUNK):
                key = f"{w['name']}:{off}"
                if key in state['done']:
                    continue
                f.seek(off)
                data = f.read(min(CHUNK, size - off))
                write_region(base + w['offset'] + off, data, key)
                state['done'][key] = hashlib.sha256(data).hexdigest()
                a.state.write_text(json.dumps(state, indent=1))
                done = off + len(data)
                print(f"{w['name']}: {done >> 20}/{size >> 20} MiB  ({time.time() - t0:.0f}s)", flush=True)
        # The manifest records the window hash from ufs-layout.json; the per-chunk readbacks above
        # already proved the bytes on disk match the image.
    if extents(a.file)[0][1] != base:
        raise SystemExit('root file moved during installation')
    write_region(base, manifest(windows, base, file_bytes), 'SHNGROOT')
    state['manifest'] = True
    a.state.write_text(json.dumps(state, indent=1))
    print(f'installed; boot with pong_shngroot_4k={base // 4096}')


if __name__ == '__main__':
    main()
