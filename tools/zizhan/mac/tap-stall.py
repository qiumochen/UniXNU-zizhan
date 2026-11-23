#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Per-gesture stall of the synthetic app-open loop (touch_lat=1 logs): for every touch-down, the longest interval without a
new panel frame in the following 600 ms. Even downs are the icon taps, odd ones the swipe home (pong_touch_synth with tsa=). usage: tap-stall.py LOG..."""
import re, sys, statistics as st
for path in sys.argv[1:]:
    t = open(path, "rb").read().decode("latin1")
    frames = sorted(int(d) for i, r, sh, d in re.findall(r"tl: v (\d+) ready (\d+) shown (\d+) done (\d+)", t))
    hid = [tuple(map(int, r)) for r in re.findall(r"tl: h type (\d+) sample (\d+) received (\d+) sent (\d+)", t)]
    downs = sorted({s for k, s, r, x in hid if k == 1})
    # the loop alternates: tap the icon, wait the period, swipe home, wait the period
    taps, homes = [], []
    for n, d in enumerate(downs):
        w = [x for x in frames if d <= x < d + 600000]
        if len(w) < 2:
            continue
        gap = max(b - a for a, b in zip(w, w[1:])) / 1000
        (homes if n % 2 else taps).append(gap)
    # the console ring may have lost the first touch-down, so the parity is not fixed: the open is the group that stalls longer
    if taps and homes and st.median(homes) > st.median(taps):
        taps, homes = homes, taps
    def s(v):
        if not v: return "n=0"
        q = sorted(v)
        return "n=%d median %.0f p25 %.0f p75 %.0f max %.0f" % (len(v), st.median(v), q[len(q) // 4], q[(3 * len(q)) // 4], q[-1])
    print("%s: tap stall ms %s | home %s" % (path.split("/")[-1], s(taps), s(homes)))
