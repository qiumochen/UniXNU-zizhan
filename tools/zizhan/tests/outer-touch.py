#!/usr/bin/env python3
"""Host tests for M2 outer touch FTM5 decoder, viewport mapper, and 2:1 cover blitter."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-outer-touch-") as temp:
    binary = Path(temp) / "outer-touch"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-I" + str(ROOT / "drivers/ArmDTPlatform"),
        str(ROOT / "tools/zizhan/tests/outer-touch.cpp"), "-o", str(binary),
    ], check=True, timeout=30)
    subprocess.run([str(binary)], check=True, timeout=10)

probe = (ROOT / "drivers/ArmDTPlatform/ZizhanOuterProbe.h").read_text()
assert 'zizhan_daily::arg("zizhan_outer_touch"' in probe
assert "ZIZHAN_OUTER_DESKTOP: SUMMARY" in probe
assert "ZIZHAN_OUTER_TOUCH_LIVE: DOWN" in probe
print("outer-touch.py: PASS")
