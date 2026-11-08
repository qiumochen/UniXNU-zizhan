#!/usr/bin/env python3
"""Replay the production panel-pool partition and slot lease model."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]

with tempfile.TemporaryDirectory(prefix="zizhan-panel-pool-") as tmp:
    binary = str(Path(tmp) / "panel-pool")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(ROOT / "tools/zizhan/tests/panel-pool.cpp"),
                    "-o", binary], check=True, timeout=30)
    subprocess.run([binary], check=True, timeout=10)
print("PASS: 96/128 MiB pool layouts, DBuf bounds, slot exhaustion, reuse, duplicate/foreign release")
