#!/usr/bin/env python3
"""Replay production DSI FIFO transport without hardware or private inputs."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class DsiCommand(unittest.TestCase):
    def test_executor_failure_and_completion_protocol(self):
        with tempfile.TemporaryDirectory(prefix="zizhan-dsi-command-") as tmp:
            binary = str(Path(tmp) / "command")
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                            "-Werror", "-fsanitize=address,undefined",
                            str(ROOT / "tools/zizhan/tests/dsi-command.cpp"), "-o", binary],
                           check=True, timeout=30)
            subprocess.run([binary], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
