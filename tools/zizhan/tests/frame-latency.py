#!/usr/bin/env python3
"""Reject missing phase/trace evidence and keep idle phase cohorts matched."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("frame_latency", ROOT / "tools/zizhan/mac/frame-latency.py")
latency = importlib.util.module_from_spec(spec)
spec.loader.exec_module(latency)


def main():
    records = """
tl: s 1 at 10000
tl: h type 1 sample 200000 received 200040 sent 200250
tl: g class SBIconScrollView state 1 at 225000
tl: s 2 at 260000
tl: v 2 ready 260050 shown 270000 done 280000
tl: h type 1 sample 400000 received 400040 sent 400250
tl: s 3 at 450000
tl: v 3 ready 450050 shown 460000 done 470000
GT s 100 100000 backboardd 1 0
GT s 101 150000 backboardd 1 0
"""
    result = latency.analyze(records)
    assert result["idle_down_to_first_panel"]["samples"] == 2
    phases = result["idle_matched_segments"]
    assert phases["samples"] == 1
    assert phases["recognition"]["median_ms"] == 25
    assert phases["gesture_to_swap"]["median_ms"] == 35
    assert phases["swap_to_panel"]["median_ms"] == 20
    assert phases["total"]["median_ms"] == 80
    assert phases["within_gpu_trace"] == 0
    overlapping = records.replace("100 100000", "100 190000").replace("101 150000", "101 290000")
    assert latency.analyze(overlapping)["idle_matched_segments"]["within_gpu_trace"] == 1
    missing = latency.analyze(records.replace("done 280000", "done 199999"))
    assert missing["idle_matched_segments"]["samples"] == 0
    assert missing["idle_matched_segments"]["total"] is None
    assert latency.analyze("")["gpu_trace_submit_window_us"] is None
    print("PASS: matched idle phases exclude missing gestures/invalid frames and expose nonoverlapping GPU evidence")


if __name__ == "__main__":
    main()
