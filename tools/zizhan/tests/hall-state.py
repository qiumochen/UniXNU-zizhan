#!/usr/bin/env python3
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]

with tempfile.TemporaryDirectory() as temp:
    binary = Path(temp) / "hall-state"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined",
        str(ROOT / "tools/zizhan/tests/hall-state.cpp"), "-o", str(binary),
    ], check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=10)
