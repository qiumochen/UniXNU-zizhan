#!/usr/bin/env python3
"""Summarize retained touch/HID/gesture/swap/panel timestamps, never fill gaps."""
import argparse
import json
from pathlib import Path
import re
import statistics


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    return {"samples": len(values), "median_ms": round(statistics.median(values), 3),
            "p90_ms": round(values[min(len(values) - 1, int(len(values) * .9))], 3)}


def analyze(text):
    swaps = {int(i): int(t) for i, t in re.findall(r"tl: s (\d+) at (\d+)", text)}
    frames = {int(i): (int(r), int(sh), int(d)) for i, r, sh, d in
              re.findall(r"tl: v (\d+) ready (\d+) shown (\d+) done (\d+)", text)}
    hid = [tuple(map(int, row)) for row in
           re.findall(r"tl: h type (\d+) sample (\d+) received (\d+) sent (\d+)", text)]
    downs = sorted({sample for kind, sample, received, sent in hid if kind == 1})
    if not downs:
        downs = sorted({int(t) for t in re.findall(r"tl: t 1 \d+ at (\d+)", text)})
    beginnings = sorted({int(t) for t in re.findall(r"tl: g class \S+ state 1 at (\d+)", text)})
    all_first, idle_first, recognition, after_recognition, swap_panel = [], [], [], [], []
    ordered_swaps = sorted(swaps.values())
    idle_segments = []
    gpu_submits = sorted(int(t) for t in re.findall(r"^GT s \d+ (\d+) \S+ \d+ \d+", text, re.M))
    for down in downs:
        candidates = sorted((t, i) for i, t in swaps.items() if down <= t < down + 500000 and i in frames)
        if not candidates:
            continue
        swap, identifier = candidates[0]
        ready, shown, done = frames[identifier]
        if not (swap <= ready <= shown <= done < down + 500000):
            continue
        all_first.append((done - down) / 1000)
        previous = [t for t in ordered_swaps if t < down]
        idle = bool(previous and down - previous[-1] >= 90000)
        if idle:
            idle_first.append((done - down) / 1000)
        began = next((t for t in beginnings if down <= t <= swap), None)
        if began is not None:
            recognition.append((began - down) / 1000)
            after_recognition.append((swap - began) / 1000)
            if idle:
                idle_segments.append({
                    "down_us": down, "began_us": began, "swap_us": swap, "panel_us": done,
                    "recognition_ms": (began - down) / 1000,
                    "gesture_to_swap_ms": (swap - began) / 1000,
                    "swap_to_panel_ms": (done - swap) / 1000,
                    "total_ms": (done - down) / 1000,
                    "gpu_trace_covers_interval": bool(gpu_submits and
                        gpu_submits[0] <= down <= done <= gpu_submits[-1]),
                })
        swap_panel.append((done - swap) / 1000)
    return {
        "retained_downs": len(downs),
        "down_to_first_panel": distribution(all_first),
        "idle_down_to_first_panel": distribution(idle_first),
        "down_to_native_pan_began": distribution(recognition),
        "native_pan_began_to_swap": distribution(after_recognition),
        "swap_to_panel": distribution(swap_panel),
        "sample_to_hid_read": distribution([(r - t) / 1000 for k, t, r, s in hid if k == 1 and t <= r <= s]),
        "hid_read_to_dispatch": distribution([(s - r) / 1000 for k, t, r, s in hid if k == 1 and t <= r <= s]),
        "idle_matched_segments": {
            "samples": len(idle_segments),
            "recognition": distribution([row["recognition_ms"] for row in idle_segments]),
            "gesture_to_swap": distribution([row["gesture_to_swap_ms"] for row in idle_segments]),
            "swap_to_panel": distribution([row["swap_to_panel_ms"] for row in idle_segments]),
            "total": distribution([row["total_ms"] for row in idle_segments]),
            "within_gpu_trace": sum(row["gpu_trace_covers_interval"] for row in idle_segments),
        },
        "gpu_trace_submit_window_us": [gpu_submits[0], gpu_submits[-1]] if gpu_submits else None,
        "idle_samples": idle_segments,
        "trace_windows": [dict(seconds=int(sec), events=int(events), submits=int(submits),
                               completions=int(done), copies=int(copies), lost=int(lost))
                          for sec, events, submits, done, copies, lost in re.findall(
                              r"PONG GTRACE: (\d+) s, (\d+) events \((\d+) submits, (\d+) completions, (\d+) scanout copies\), (\d+) lost", text)],
        "note": "Retained matching records only; idle means at least 90 ms without a prior swap. Screenshot/performance trials should be separate."
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(args.log.read_text(errors="replace")), indent=2))


if __name__ == "__main__":
    main()
