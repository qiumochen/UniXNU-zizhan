#!/usr/bin/env python3
"""Replay the sensor DSP's apps_std file traffic against drivers/PongPower/ZizhanSlpiStd.h with sanitizer checks."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class SlpiStd(unittest.TestCase):
    def test_registry_file_traffic(self):
        with tempfile.TemporaryDirectory(prefix="zizhan-slpi-std-") as tmp:
            binary = str(Path(tmp) / "std")
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17",
                            "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined",
                            str(ROOT / "tools/zizhan/tests/slpi-std.cpp"),
                            "-o", binary], check=True, timeout=60)
            subprocess.run([binary], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
