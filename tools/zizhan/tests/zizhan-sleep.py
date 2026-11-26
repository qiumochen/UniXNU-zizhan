#!/usr/bin/env python3
"""Host unit test runner for ArmCpuSleep.h and ZizhanSleepPlan.h (Track 1 POWER-02)."""
import os
import shutil
import subprocess
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
SRC = os.path.join(ROOT, "tools/zizhan/tests/zizhan-sleep.c")
CC = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")

with tempfile.TemporaryDirectory() as td:
    exe = os.path.join(td, "zizhan-sleep")
    subprocess.check_call([
        CC, "-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", SRC, "-o", exe,
    ])
    out = subprocess.check_output([exe], text=True).strip()
    assert "PASS zizhan-sleep" in out, out
    print(out)
