#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""One-shot summary of a zizhan device log (the supervisor's logs/<run>.txt). Host only, read-only.

Usage: zlog.py LOG [--ping PINGLOG] [--grep REGEX ...] [--max N]

Sections (each only when the log has it): boot args of interest, display transitions, panics, battery current by display
state and USB presence, CPU offline transitions, CPU load (pong_cpu_load_log), busiest processes (zizhan_top), GPU DCVS,
thermal maxima, Wi-Fi address, frame rate, and every --grep match (deduplicated, counted). --ping summarises a ping log
written as "<epoch> <ping line>" (replies, gaps over 2 s, RTT).
"""
import argparse
import collections
import re
import statistics as st
import sys

INTEREST = ("zizhan_", "pong_cpu", "pong_gpu", "pong_astc", "pong_autoreset", "processor_exit", "pong_adsp", "applog")


def text(path):
    return open(path, "rb").read().decode("latin1")


def median(v):
    return st.median(v) if v else None


def section(title):
    print(f"== {title}")


def boot_args(t, path):
    m = re.search(r"boot-args = ([^\r\n]*)", t)
    args = m.group(1) if m else ""
    if not args:   # fall back to the supervisor record next to the log
        try:
            import json
            args = json.load(open(re.sub(r"\.txt$", ".supervisor.json", path))).get("kernel_args", "")
        except (OSError, ValueError):
            return
    keep = [a for a in args.split() if a.startswith(INTEREST)]
    section("boot args")
    print("  " + " ".join(keep))


def display_and_battery(t):
    state, usb = "on", "?"
    trans = collections.Counter()
    cur = collections.defaultdict(list)
    temp = collections.defaultdict(list)
    pct = []
    for m in re.finditer(r"ZIZHAN_DISPLAY_POWER: (PASS|FAIL) target=(on|off)|PongBattery: (\d+)% \d+mV (-?\d+)mA temp (\d+) flags 0x\w+ usb (-?\d+)", t):
        if m.group(1):
            trans[m.group(1) + " " + m.group(2)] += 1
            if m.group(1) == "PASS":
                state = m.group(2)
            continue
        pct.append(int(m.group(3)))
        usb = m.group(6)
        key = f"display {state}, usb {usb}"
        cur[key].append(int(m.group(4)))
        temp[key].append(int(m.group(5)))
    if trans:
        section("display transitions")
        print("  " + ", ".join(f"{k}: {v}" for k, v in sorted(trans.items())))
    if cur:
        section("battery current (mA, negative = discharge)")
        for k in sorted(cur):
            print(f"  {k}: samples {len(cur[k])}, median {median(cur[k])}, min {min(cur[k])}, max {max(cur[k])}, temp {temp[k][0]}->{temp[k][-1]}")
        print(f"  charge {pct[0]}% -> {pct[-1]}%")


def panics(t):
    p = [l for l in re.findall(r"panic\(cpu[^\r\n]*", t)]
    section("panics")
    print(f"  {len(p)}" + ("".join("\n  " + x[:200] for x in list(dict.fromkeys(p))[:5]) if p else ""))


def offline(t):
    rows = re.findall(r"ZIZHAN_CPU_OFFLINE: display (off|on) -> (exit|start) cpus([^\r\n]*?) offline_mask=(\w+) in (\d+) ms", t)
    if not rows:
        return
    bad = [r for r in rows if re.search(r"\d+:(?!0\b)-?\d+", r[2])]
    ms = collections.defaultdict(list)
    for r in rows:
        ms[r[1]].append(int(r[4]))
    section("CPU offline")
    print(f"  transitions {len(rows)} (exit {len(ms['exit'])}, start {len(ms['start'])}), failed {len(bad)}, "
          f"exit ms median {median(ms['exit'])}, start ms median {median(ms['start'])}")


def load(t):
    rows = re.findall(r"Pong CPU: load 30s busy% ([\d ]+) clock (\d+)/(\d+)/(\d+) MHz", t)
    if not rows:
        return
    section("CPU load (30 s windows, last 6)")
    for r in rows[-6:]:
        print(f"  busy% {r[0].strip()}  clock {r[1]}/{r[2]}/{r[3]} MHz")


def top(t, n):
    rows = re.findall(r"ZIZHAN_TOP: 30s cpu-ms total=(\d+)([^\r\n]*)", t)
    if not rows:
        return
    acc = collections.Counter()
    for _, rest in rows:
        for name, ms in re.findall(r" (\S+?)=(\d+)", rest):
            acc[name] += int(ms)
    section(f"busiest processes over {len(rows)} windows (cpu-ms; totals median {median([int(r[0]) for r in rows])})")
    print("  " + ", ".join(f"{k} {v}" for k, v in acc.most_common(n)))


def gpu(t):
    rows = re.findall(r"PONG GPU DCVS: busy (\d+)%, level (\d+) \((\d+) MHz\)[^\r\n]*?(\d+) changes", t)
    if not rows:
        return
    section("GPU DCVS")
    print(f"  samples {len(rows)}, busy median {median([int(r[0]) for r in rows])}%, MHz median {median([int(r[2]) for r in rows])}, "
          f"last changes {rows[-1][3]}")


def thermal(t):
    rows = re.findall(r"ZIZHAN_THERMAL: summary max_cpu=(\d+) max_gpu=(\d+) cap=(\d+)/(\d+)", t)
    if not rows:
        return
    section("thermal")
    print(f"  max cpu {max(int(r[0]) for r in rows) / 1000:.1f} C, max gpu {max(int(r[1]) for r in rows) / 1000:.1f} C, "
          f"capped windows {sum(1 for r in rows if r[2] != '0' or r[3] != '0')}/{len(rows)}")


def wifi_and_frames(t):
    a = sorted(set(re.findall(r"wifi-net: en0 address (\d+\.\d+\.\d+\.\d+)", t)))
    if a:
        section("Wi-Fi")
        print("  address " + ", ".join(a))
    f = [int(x) for x in re.findall(r"MiniIOMobileFramebuffer: (\d+) frames in 5\d\d\d ms", t)]
    if f:
        section("frames per 5 s (MiniIOMobileFramebuffer)")
        print(f"  windows {len(f)}, median {median(f)}, last 8: {f[-8:]}")


def greps(t, patterns, n):
    for p in patterns:
        c = collections.Counter(m.group(0)[:220] for m in re.finditer(p + r"[^\r\n]*", t))
        section(f"grep {p} ({sum(c.values())} lines, {len(c)} distinct)")
        for line, k in c.most_common(n):
            print(f"  {k}x {line}")


def ping(path):
    ok, rtt, found = [], [], None
    for l in open(path, errors="ignore"):
        p = l.split()
        if not p or not p[0].isdigit():
            continue
        if "FOUND" in l:
            found = l.strip()
        m = re.search(r"time=([\d.]+)", l)
        if m:
            ok.append(int(p[0])); rtt.append(float(m.group(1)))
    section("ping")
    if found:
        print("  " + found)
    if not ok:
        print("  no replies"); return
    gaps = [(a, b - a) for a, b in zip(ok, ok[1:]) if b - a > 2]
    print(f"  replies {len(ok)} over {ok[-1] - ok[0]} s, gaps>2s {len(gaps)}, rtt median {median(rtt):.1f} ms")
    for a, g in gaps[:10]:
        print(f"  gap after {a} for {g} s")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--ping")
    ap.add_argument("--grep", action="append", default=[])
    ap.add_argument("--max", type=int, default=12)
    a = ap.parse_args()
    t = text(a.log)
    boot_args(t, a.log); panics(t); display_and_battery(t); offline(t); load(t); top(t, a.max); gpu(t); thermal(t); wifi_and_frames(t)
    greps(t, a.grep, a.max)
    if a.ping:
        ping(a.ping)


if __name__ == "__main__":
    sys.exit(main())
