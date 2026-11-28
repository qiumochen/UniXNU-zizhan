#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Replay whole-segment physical span coalescing and GPU health MMIO poll rate-limiting."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]

with tempfile.TemporaryDirectory(prefix="zizhan-span-collect-") as tmp:
    binary = str(Path(tmp) / "adreno-span-collect")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(ROOT / "tools/zizhan/tests/adreno-span-collect.cpp"),
                    "-o", binary], check=True, timeout=30)
    subprocess.run([binary], check=True, timeout=10)
print("PASS: contiguous segment stride, scattered growth bounds, PA validation, and GPU health poll gate")
