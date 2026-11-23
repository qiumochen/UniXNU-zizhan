#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Profile of the app-open stall from a ZIZHAN_SAMPLE round (XNU patch 0036, boot-args zizhan_sample=400 zizhan_sample_ipi=1
zizhan_sample_us=1000 with pong_touch_synth and tsa=). Host only (macOS: nm and atos).

Usage: sample-profile.py LOG KERNEL [DSC_DIR] [--stacks]

Per tap: milliseconds in user mode, in the kernel, queued and waiting. Then the user samples by library and function (leaf and
inclusive over the first frames), the kernel samples by the user call that entered the kernel and by kernel function, and, for
the taps that carry deep frames, a root-first call tree. KERNEL is the unstripped mach.development binary of that round; DSC_DIR
an extracted dyld shared cache (see dscsym.py). --stacks also lists the kernel backtraces.
"""
import bisect, collections, importlib.util, os, re, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
if len(sys.argv) < 3:
    sys.exit(__doc__)
log, kernel = sys.argv[1], sys.argv[2]
dsc = sys.argv[3] if len(sys.argv) > 3 and not sys.argv[3].startswith("--") else "work/dsc-22H374"
spec = importlib.util.spec_from_file_location("dscsym", os.path.join(here, "dscsym.py"))
d = importlib.util.module_from_spec(spec); spec.loader.exec_module(d)
t = open(log, "rb").read().decode("latin1")
slide = int(re.search(r"ZIZHAN_SAMPLE \d+: tid .*cache_slide=0x([0-9a-f]+)", t).group(1), 16)
kslide = int(re.search(r"ZIZHAN_SAMPLE \d+ ipi: kernel_slide=0x([0-9a-f]+)", t).group(1), 16)
step = int(re.search(r"ZIZHAN_SAMPLE \d+: tid \d+ step (\d+) us", t).group(1))
# kernel symbols
syms = []
for l in subprocess.run(["nm", "-n", kernel], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3 and p[1] in "tTsS":
        syms.append((int(p[0], 16), p[2]))
addrs = [a for a, _ in syms]
def ksym(a):
    a -= kslide
    i = bisect.bisect_right(addrs, a) - 1
    if i < 0 or a - addrs[i] > 0x40000: return "?k%x" % a
    return syms[i][1]
modes = collections.defaultdict(dict)
for m in re.finditer(r"ZIZHAN_SAMPLE (\d+) mode (\d+): (\S+)", t):
    modes[int(m.group(1))][int(m.group(2))] = m.group(3)
ust, kst = {}, {}
for m in re.finditer(r"ZIZHAN_SAMPLE (\d+) st (\d+) (\S): ((?:0x[0-9a-f]+ ?)+)", t):
    ust[(int(m.group(1)), int(m.group(2)))] = (m.group(3), [int(x, 16) for x in m.group(4).split()])
for m in re.finditer(r"ZIZHAN_SAMPLE (\d+) kst (\d+): ((?:0x[0-9a-f]+ ?)+)", t):
    kst[(int(m.group(1)), int(m.group(2)))] = [int(x, 16) for x in m.group(3).split()]
dst = collections.defaultdict(dict)
for m in re.finditer(r"ZIZHAN_SAMPLE (\d+) dst (\d+) (\d+): ((?:[0-9a-f]+ ?)+)", t):
    dst[(int(m.group(1)), int(m.group(2)))][int(m.group(3))] = [int(x, 16) for x in m.group(4).split()]
for key, parts in dst.items():
    if key in ust and ust[key][0] == "U":
        extra = [a for k in sorted(parts) for a in parts[k] if a]
        ust[key] = ("U", [a for a in ust[key][1] if a] + extra)
print("step %d us, windows %d, user-stack samples %d, kernel samples %d" % (step, len(modes), len(ust), len(kst)))
tot = collections.Counter()
for tag in sorted(modes):
    s = "".join(modes[tag][k] for k in sorted(modes[tag]))
    c = collections.Counter(s)
    if tag % 2 == 0:
        tot.update(c)
        first = next((i for i, ch in enumerate(s) if ch in "UKqx" and i > 60000 // step), None)
        print("tap %2d: U %3d K %3d q %3d x %3d w %3d  (ms: user %.0f kernel %.0f)  %s" % (tag, c["U"], c["K"], c["q"], c["x"], c["w"], c["U"] * step / 1000, c["K"] * step / 1000, ""))
n = sum(tot.values()) or 1
print("ALL TAPS: " + "  ".join("%s %d (%.0f%%)" % (k, tot[k], 100 * tot[k] / n) for k in "UKqxw"))
rows = d.index(dsc)
alluser = sorted({a - slide for (_, st) in ust.values() for a in st if a})
names = d.symbolize(dsc, rows, alluser)
def ufn(a):
    nme = names.get(a - slide, "?")
    if nme == "?": return "?%x" % a
    return re.sub(r" \(in [^)]*\) \+ \d+$", "", nme)
def top(c, k, width=150):
    for name, cnt in c.most_common(k): print("  %4d %5.1f%%  %s" % (cnt, 100 * cnt / max(1, sum_total[0]), name[:width]))
sum_total = [tot["U"] + tot["K"]]
uex = collections.Counter(); uin = collections.Counter(); ulib = collections.Counter()
kex = collections.Counter(); kin = collections.Counter(); kentry = collections.Counter(); kentry_fn = collections.defaultdict(collections.Counter)
for key, (mode, st) in ust.items():
    if key[0] % 2: continue
    st = [a for a in st if a]
    if mode == "U" and st:
        f = ufn(st[0]); uex[f] += 1; ulib[f.split(":")[0] if ":" in f else f[:4]] += 1
        for g in set(ufn(a) for a in st): uin[g] += 1
    elif mode == "K":
        ent = ufn(st[0]) if st else "?"
        ks = [a for a in kst.get(key, []) if a]
        kentry[ent] += 1
        if ks:
            kex[ksym(ks[0])] += 1
            for g in set(ksym(a) for a in ks): kin[g] += 1
            kentry_fn[ent][ksym(ks[0])] += 1
print("== user-mode samples by library"); top(ulib, 15)
print("== user exclusive"); top(uex, 30)
print("== user inclusive"); top(uin, 30)
print("== kernel samples by entry (user pc at the trap)"); top(kentry, 15)
print("== kernel exclusive (interrupted pc)"); top(kex, 40)
print("== kernel inclusive"); top(kin, 50)
for ent, _ in kentry.most_common(6):
    print("== kernel pcs under", ent[:80])
    for name, cnt in kentry_fn[ent].most_common(8): print("  %4d %s" % (cnt, name))
if "--stacks" in sys.argv:
    c = collections.Counter()
    for key, ks in kst.items():
        if key[0] % 2 == 0: c[" < ".join(ksym(a) for a in ks if a)] += 1
    print("== kernel stacks")
    for name, cnt in c.most_common(40): print("  %4d %s" % (cnt, name[:300]))

if dst:
    # root-first call tree of the U samples that carry deep frames
    tree = {}
    total = 0
    for key, (mode, st) in ust.items():
        if mode != "U" or key not in dst and len([a for a in st if a]) >= 9: continue
        if key[0] % 2 or not (2 <= key[0] <= 8): continue
        frames = [ufn(a).split(": ", 1)[-1] for a in st if a][::-1]
        total += 1
        node = tree
        for f in frames:
            node = node.setdefault(f, [0, {}])
            node[0] += 1
            node = node[1]
    floor = max(3, total * 0.03)
    print("== call tree, root first (%d user samples of taps 1-4; nodes under %.0f samples dropped)" % (total, floor))
    def walk(node, depth):
        for name, (cnt, kids) in sorted(node.items(), key=lambda kv: -kv[1][0]):
            if cnt < floor: continue
            # collapse a chain of single dominant children
            chain = [name]
            while True:
                big = [(n2, v2) for n2, v2 in kids.items() if v2[0] >= floor]
                if len(big) == 1 and big[0][1][0] >= cnt * 0.9 and len(chain) < 4:
                    chain.append(big[0][0]); cnt2, kids = big[0][1]; cnt = cnt2
                else: break
            print("%s%4d %4.1f%% %s" % ("  " * min(depth, 30), cnt, 100 * cnt / total, " > ".join(c[:70] for c in chain)))
            walk(kids, depth + 1)
    walk(tree, 0)
