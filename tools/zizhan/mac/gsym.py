#!/usr/bin/env python3
"""Mac: symbolicate whole gesture-sampler records (PONG GS b ...), one stack per thread, for a gs_all round.
usage: gsym.py LOG GESTURE [TID ...] [--bin PATH]   (default: every record of the gesture with >= 1000 samples)
The shared-cache slide comes from the round's 'PONG GS g' line; frames below 0x180000000 are the process's own image, resolved with
atos against --bin when given (the binary copied out of the OS image). ZIZHAN_DSC names the 22H374 shared cache."""
import os, re, subprocess, sys, shutil
args = sys.argv[1:]
binary = args[args.index('--bin') + 1] if '--bin' in args else None
args = [a for i, a in enumerate(args) if a != '--bin' and (i == 0 or args[i - 1] != '--bin')]
log, gesture, tids = args[0], args[1], set(args[2:])
dsc = os.environ.get('ZIZHAN_DSC', os.path.expanduser('~/zizhan/apple.noindex/dsc/22H374__iPad7,11_12/dyld_shared_cache_arm64'))
t = open(log, 'rb').read().decode('latin-1')
slide = int(re.findall(r'PONG GS g \d+ down \d+ ms \d+ slide (0x[0-9a-f]+)', t)[-1], 16)
img = [(int(a, 16), p) for a, p in re.findall(r'PONG GS img (0x[0-9a-f]+) (\S+)', t)]
main = next((a for a, p in img if binary and p.endswith('/' + binary.split('/')[-1])), None)
ipsw = shutil.which('ipsw') or '/opt/homebrew/bin/ipsw'
cache = {}
def sym(pc):
    if pc in cache: return cache[pc]
    if pc < 0x180000000:
        r = subprocess.run(['atos', '-o', binary, '-l', hex(main), hex(pc)], capture_output=True, text=True).stdout.strip() if binary and main else '?'
    else:
        out = subprocess.run([ipsw, 'dyld', 'a2s', dsc, hex(pc), '--slide', str(slide), '--no-color'], capture_output=True, text=True).stdout.strip().splitlines()
        r = out[-1].strip() if out else '?'
    cache[pc] = r; return r
for m in re.finditer(rf'PONG GS b {gesture} \d+ n (\d+) ms \S+ (\S) .*? t (\d+) (\S+) u ([0-9a-f ]+)', t):
    n, kind, tid, name, us = m.groups()
    if (tids and tid not in tids) or (not tids and int(n) < 1000): continue
    print(f'== tid {tid} {name} {kind} x{n}')
    for a in us.split():
        print(f'   {sym(int(a, 16))}')
