#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Replay per-swap zero-copy GPU fence target selection and wrap-safe wait bounds."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]

with tempfile.TemporaryDirectory(prefix="zizhan-zc-fence-") as tmp:
    binary = str(Path(tmp) / "zc-fence-target")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(ROOT / "tools/zizhan/tests/zc-fence-target.cpp"),
                    "-o", binary], check=True, timeout=30)
    subprocess.run([binary], check=True, timeout=10)
print("PASS: SwapEnd compositor fence snapshot, non-chasing poll loop, legacy control, 32-bit wrap")
