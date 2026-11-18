#!/usr/bin/env python3
"""Host tests for the POWER-02 S1 display-power request state machine."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-display-power-") as temp:
    binary = Path(temp) / "display-power"
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-I" + str(root / "drivers/ArmDTPlatform"),
        str(root / "tools/zizhan/tests/display-power.c"), "-o", str(binary),
    ], check=True, timeout=30)
    result = subprocess.run([str(binary)], timeout=10, text=True, capture_output=True)
    print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="")
    if result.returncode != 0:
        raise SystemExit(result.returncode)
