#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Per-phase frame cadence of synthetic app-open/home-swipe loops (touch_lat=1 logs).

Separates four windows hidden by a single max-gap metric:
  1. open_blank : longest pre-zoom gap in the first 350 ms after an icon tap (and post-finger-up gap = blank - 60 ms)
  2. open_zoom  : frame-to-frame intervals in the 500 ms zoom window following open_blank
  3. exit_zoom  : frame-to-frame intervals in [+40 ms, +530 ms] after swipe-home touch-down
  4. home_settle: frame-to-frame intervals in [+550 ms, +880 ms] after swipe-home touch-down

Also extracts PONG ZC deadline fence/latch waits, canvas slot NONE FREE misses, and user SIGABRT crashes.
usage: zoom-cadence.py LOG...
"""
import bisect
import os
import re
import statistics as st
import sys


def fmt_phase(name, dts, waits):
    if not dts:
        return f"{name}: n=0"
    s = sorted(dts)
    p90 = s[min(len(s) - 1, int(0.9 * len(s)))]
    le10 = sum(1 for x in dts if x <= 10.5)
    gt25 = sum(1 for x in dts if x > 25.0)
    gt33 = sum(1 for x in dts if x > 33.0)
    fps = 1000.0 / st.mean(dts)
    w_avg = st.mean(waits) if waits else 0.0
    w_max = max(waits) if waits else 0.0
    return (
        f"{name}: n={len(dts)} fps={fps:.1f} dt(med={st.median(dts):.1f} p90={p90:.1f} max={s[-1]:.1f}ms) "
        f"120Hz={100 * le10 / len(dts):.0f}% >25ms={100 * gt25 / len(dts):.0f}% >33ms={100 * gt33 / len(dts):.0f}% "
        f"show_wait(avg={w_avg:.1f} max={w_max:.1f}ms)"
    )


def analyze(path):
    text = open(path, "rb").read().decode("utf-8", errors="replace").replace("\r", "")
    hid = [(int(tp), int(samp)) for tp, samp, _, _ in re.findall(r"tl: h type (\d+) sample (\d+) received (\d+) sent (\d+)", text)]
    frames = [(int(i), int(r), int(sh), int(d)) for i, r, sh, d in re.findall(r"tl: v (\d+) ready (\d+) shown (\d+) done (\d+)", text)]
    frames.sort(key=lambda x: x[3])
    dones = [f[3] for f in frames]

    gestures = []
    for tp, samp in hid:
        if not gestures or samp - gestures[-1][-1][1] > 500_000:
            gestures.append([(tp, samp)])
        else:
            gestures[-1].append((tp, samp))

    open_blanks = []
    open_dts, open_waits = [], []
    exit_dts, exit_waits = [], []
    settle_dts, settle_waits = [], []

    for g in gestures:
        t0 = g[0][1]
        has_move = any(x[0] == 2 for x in g)
        i0 = bisect.bisect_left(dones, t0)
        i1 = bisect.bisect_right(dones, t0 + 900_000)
        sub = frames[i0:i1]
        if len(sub) < 4:
            continue
        if not has_move:
            early = [f for f in sub if f[3] <= t0 + 350_000]
            if len(early) >= 2:
                gaps = [((early[k + 1][3] - early[k][3]) / 1000.0, early[k + 1][3]) for k in range(len(early) - 1)]
                max_gap, zoom_start = max(gaps, key=lambda x: x[0])
            else:
                max_gap = (sub[0][3] - t0) / 1000.0
                zoom_start = sub[0][3]
            if max_gap > 80.0:
                open_blanks.append(max_gap)
                zsub = [f for f in sub if zoom_start <= f[3] <= zoom_start + 500_000]
                for k in range(len(zsub) - 1):
                    dt = (zsub[k + 1][3] - zsub[k][3]) / 1000.0
                    wt = (zsub[k + 1][3] - zsub[k + 1][2]) / 1000.0
                    if dt < 150.0:
                        open_dts.append(dt)
                        open_waits.append(wt)
        else:
            for k in range(len(sub) - 1):
                rel = (sub[k + 1][3] - t0) / 1000.0
                dt = (sub[k + 1][3] - sub[k][3]) / 1000.0
                wt = (sub[k + 1][3] - sub[k + 1][2]) / 1000.0
                if 40.0 <= rel <= 530.0 and dt < 150.0:
                    exit_dts.append(dt)
                    exit_waits.append(wt)
                elif 550.0 <= rel <= 880.0 and dt < 150.0:
                    settle_dts.append(dt)
                    settle_waits.append(wt)

    none_free = text.count("canvas slot NONE FREE")
    aborts = len(re.findall(r"exited due to SIGABRT", text))
    zc_lines = re.findall(r"PONG ZC [^\n]+", text)
    zc_tail = zc_lines[-1] if zc_lines else "no PONG ZC summary"

    label = os.path.basename(path)
    print(f"=== {label} ===")
    if open_blanks:
        sb = sorted(open_blanks)
        p90b = sb[min(len(sb) - 1, int(0.9 * len(sb)))]
        med = st.median(open_blanks)
        print(f"  open_blank : n={len(open_blanks)} med={med:.1f}ms (post-up ~{med - 60.0:.1f}ms) p90={p90b:.1f}ms max={sb[-1]:.1f}ms")
    else:
        print("  open_blank : n=0")
    print("  " + fmt_phase("open_zoom  ", open_dts, open_waits))
    print("  " + fmt_phase("exit_zoom  ", exit_dts, exit_waits))
    print("  " + fmt_phase("home_settle", settle_dts, settle_waits))
    print(f"  health     : slot_none_free={none_free} sigabrt={aborts} | {zc_tail}")


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        analyze(arg)
