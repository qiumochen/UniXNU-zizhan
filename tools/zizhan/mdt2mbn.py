#!/usr/bin/env python3
"""Mac: merge a Qualcomm split firmware (NAME.mdt + NAME.bNN) into one NAME.mbn, as pil-squasher does: the ELF headers from the .mdt,
every program header's bytes from its .bNN at its p_offset. Prints the segments' load range (it must fit the carveout).
usage: mdt2mbn.py DIR/NAME.mdt OUT.mbn"""
import os, struct, sys

mdt, out = sys.argv[1], sys.argv[2]
base = mdt[:-4]
head = open(mdt, 'rb').read()
assert head[:4] == b'\x7fELF', 'not an ELF'
is64 = head[4] == 2
if is64:
    phoff, = struct.unpack_from('<Q', head, 0x20); phentsize, phnum = struct.unpack_from('<HH', head, 0x36)
else:
    phoff, = struct.unpack_from('<I', head, 0x1c); phentsize, phnum = struct.unpack_from('<HH', head, 0x2a)
segs = []
for i in range(phnum):
    o = phoff + i * phentsize
    if is64:
        ptype, flags, off, vaddr, paddr, filesz, memsz = struct.unpack_from('<IIQQQQQ', head, o)
    else:
        ptype, off, vaddr, paddr, filesz, memsz, flags = struct.unpack_from('<IIIIIII', head, o)
    segs.append((i, ptype, flags, off, paddr, filesz, memsz))
size = max(off + filesz for _, _, _, off, _, filesz, _ in segs)
blob = bytearray(size)
blob[:len(head)] = head
for i, ptype, flags, off, paddr, filesz, memsz in segs:
    if not filesz:
        continue
    part = f'{base}.b{i:02d}'
    if os.path.exists(part):
        data = open(part, 'rb').read()
        assert len(data) == filesz, (part, len(data), filesz)
        blob[off:off + filesz] = data
    elif off + filesz <= len(head):
        pass  # the .mdt itself carries it (headers, hash segment)
    else:
        sys.exit(f'missing {part}')
open(out, 'wb').write(blob)
load = [(paddr, memsz) for _, ptype, flags, off, paddr, filesz, memsz in segs if ptype == 1 and memsz]
lo = min(p for p, _ in load); hi = max(p + m for p, m in load)
print(f'{out}: {len(blob)} bytes, {phnum} segments, load 0x{lo:x}-0x{hi:x} (span 0x{hi - lo:x})')
