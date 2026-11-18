#!/usr/bin/env python3
"""Host tests for the THERM-01 policy core (header-only, no kernel APIs)."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-thermal-") as temp:
    binary = Path(temp) / "thermal-policy"
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-I" + str(root / "drivers/ArmDTPlatform"),
        str(root / "tools/zizhan/tests/thermal-policy.c"), "-o", str(binary),
    ], check=True, timeout=30)
    result = subprocess.run([str(binary)], timeout=10, text=True, capture_output=True)
    print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="")
    if result.returncode != 0:
        raise SystemExit(result.returncode)
