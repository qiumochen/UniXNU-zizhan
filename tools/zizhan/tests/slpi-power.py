#!/usr/bin/env python3
"""Compile the production SLPI resource plan/lease core with ASan/UBSan."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-slpi-power-") as temp:
    binary = Path(temp) / "test"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I" + str(ROOT / "drivers/PongPower"),
                    str(ROOT / "drivers/PongPower/ZizhanSlpiPower.c"),
                    str(ROOT / "tools/zizhan/tests/slpi-power.c"), "-o", str(binary)],
                   check=True, timeout=30)
    subprocess.run([str(binary)], check=True, timeout=10)
