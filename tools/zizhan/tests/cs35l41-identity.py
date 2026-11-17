#!/usr/bin/env python3
"""Actual CS35L41 identity and GENI transaction core under ASan/UBSan."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-cs35l41-") as temp:
    binary = Path(temp) / "identity"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I" + str(ROOT / "drivers/PongPower"),
                    str(ROOT / "drivers/PongPower/GeniI2c.c"),
                    str(ROOT / "drivers/PongPower/ZizhanCs35l41.c"),
                    str(ROOT / "tools/zizhan/tests/cs35l41-identity.c"), "-o", str(binary)],
                   check=True, timeout=30)
    subprocess.run([str(binary)], check=True, timeout=10)
    reset = Path(temp) / "reset"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I" + str(ROOT / "drivers/PongPower"),
                    str(ROOT / "tools/zizhan/tests/cs35l41-reset.cpp"), "-o", str(reset)], check=True, timeout=30)
    subprocess.run([str(reset)], check=True, timeout=10)
