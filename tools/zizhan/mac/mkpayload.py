#!/usr/bin/env python3
"""Mac: splice a UniXNU FAT payload (PONGFAT1 envelope) into the zizhan UEFI boot image.

usage: mkpayload.py OUT.img FILE[=NAME] ... --toml TEXT
Files land in EFI/UniXNU/<NAME>. Mirrors UniXNU-pong scripts/build-ipados18-bootimg-local.py.
"""
import argparse, os, struct, subprocess, tempfile, zlib
from pathlib import Path
SRC = Path(os.environ.get('ZIZHAN_SRC', Path.home() / 'zizhan'))
p = argparse.ArgumentParser()
p.add_argument('out', type=Path)
p.add_argument('files', nargs='+')
p.add_argument('--toml', required=True)
p.add_argument('--template', type=Path, default=SRC / 'out/zizhan-uefi.img')
p.add_argument('--fat-mib', type=int, default=32)
a = p.parse_args()
env = dict(os.environ, MTOOLS_SKIP_CHECK='1', TZ='UTC')
with tempfile.TemporaryDirectory() as t:
    t = Path(t); stage = t / 'EFI/UniXNU'; stage.mkdir(parents=True)
    for f in a.files:
        src, _, name = f.partition('=')
        (stage / (name or Path(src).name)).write_bytes(Path(src).read_bytes())
    (stage / 'unixnu.toml').write_text(a.toml)
    fat = t / 'fat.img'
    subprocess.run(['mformat', '-C', '-T', str(a.fat_mib * 2048), '-v', 'ZIZHANPL', '-i', str(fat), '::'], check=True, env=env)
    subprocess.run(['mcopy', '-i', str(fat), '-s', str(t / 'EFI'), '::/'], check=True, env=env)
    raw = fat.read_bytes()
hdr = bytearray(4096)
struct.pack_into('<8sIIQII', hdr, 0, b'PONGFAT1', 1, 4096, len(raw), zlib.crc32(raw), 0)
ramdisk = bytes(hdr) + raw
tpl = a.template.read_bytes()
assert tpl[:8] == b'ANDROID!' and struct.unpack_from('<I', tpl, 40)[0] == 4
ksz, old = struct.unpack_from('<2I', tpl, 8)
page = lambda n: (n + 4095) & ~4095
rd_at = 4096 + page(ksz); sig_at = rd_at + page(old)
assert len(tpl) == sig_at + 4096 and not any(tpl[sig_at:])
img = bytearray(tpl[:rd_at]); struct.pack_into('<I', img, 12, len(ramdisk))
img += ramdisk + bytes(page(len(ramdisk)) - len(ramdisk)) + tpl[sig_at:]
a.out.parent.mkdir(parents=True, exist_ok=True); a.out.write_bytes(img)
print(a.out, len(img))
