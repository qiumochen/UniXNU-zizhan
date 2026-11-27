#!/usr/bin/env python3
"""Host unit test runner for ZizhanHapticsCore.h (ASan + UBSan)."""
import os, subprocess, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = Path(__file__).with_suffix(".c")

with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / "zizhan-haptics"
    cc = os.environ.get("CC", "clang")
    subprocess.run([cc, "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(SRC), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
