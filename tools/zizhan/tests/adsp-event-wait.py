#!/usr/bin/env python3
"""Replay actual zizhan ADSP event-wait code with sanitizer checks."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class AdspEventWait(unittest.TestCase):
    def test_arrival_races_and_timeout_policy(self):
        with tempfile.TemporaryDirectory(prefix="zizhan-adsp-wait-") as tmp:
            binary = str(Path(tmp) / "wait")
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17",
                            "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined",
                            str(ROOT / "tools/zizhan/tests/adsp-event-wait.cpp"),
                            "-o", binary], check=True, timeout=30)
            subprocess.run([binary], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
