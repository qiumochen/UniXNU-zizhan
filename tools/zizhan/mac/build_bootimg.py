#!/usr/bin/env python3
"""Mac: pack zizhan SILICIUM_UEFI.fd + DTB into an Android boot image v4 (fastboot boot only)."""
import os
import gzip, hashlib, subprocess, sys, tempfile, tomllib
from pathlib import Path
SRC = Path(os.environ.get('ZIZHAN_SRC', Path.home() / 'zizhan'))
U = SRC / 'uefi-zizhan'
sys.path.insert(0, str(SRC / 'UniXNU-pin/scripts'))
from unixnu.bootimg import compile_shim
target = sys.argv[1] if len(sys.argv) > 1 else 'RELEASE'
out = Path(sys.argv[2]) if len(sys.argv) > 2 else SRC / 'out/zizhan-uefi.img'
ramdisk = Path(sys.argv[3]).read_bytes() if len(sys.argv) > 3 else b''
cfg = tomllib.loads((U / 'Resources/Configs/zizhan.toml').read_text())
base, size = cfg['uefi_fd']['base'], cfg['uefi_fd']['size']
fd = (U / f'Build/zizhanPkg/{target}_CLANGPDB/FV/SILICIUM_UEFI.fd').read_bytes()
assert len(fd) == size, hex(len(fd))
dtb = (U / 'Resources/DTBs/zizhan.dtb').read_bytes()
out.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory() as t:
    t = Path(t)
    shim = compile_shim(U, base, size, True, t)
    (t / 'kernel').write_bytes(gzip.compress(shim + fd, mtime=0) + dtb)
    (t / 'ramdisk').write_bytes(ramdisk)
    subprocess.run([sys.executable, str(U / 'Resources/Scripts/mkbootimg.py'), '--header_version', '4',
                    '--kernel', str(t / 'kernel'), '--ramdisk', str(t / 'ramdisk'), '-o', str(out)], check=True)
print(out, out.stat().st_size, hashlib.sha256(out.read_bytes()).hexdigest())
