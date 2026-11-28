#!/usr/bin/env python3
"""Host ASan/UBSan test runner for userland/zizhan-ipa-install18.h."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


class IpaInstall18Tests(unittest.TestCase):
    def test_c_harness(self):
        cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
        self.assertIsNotNone(cc)
        src = Path(__file__).resolve().with_name("ipa-install18.c")
        with tempfile.TemporaryDirectory(prefix="zizhan-ipa-test-") as td:
            exe = Path(td) / "ipa-install18"
            subprocess.run(
                [cc, "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", str(src), "-o", str(exe)],
                check=True,
            )
            out = subprocess.check_output([str(exe)], text=True)
            self.assertIn("PASS ipa-install18", out)


if __name__ == "__main__":
    unittest.main()
