#!/usr/bin/env python3
"""Host ASan/UBSan + CoreImage QR roundtrip test for userland/zizhan-cam-proto.h."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


class AvCapture18Tests(unittest.TestCase):
    def test_c_isp_and_ae_awb_harness(self):
        cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc") or shutil.which("gcc")
        self.assertIsNotNone(cc)
        src = Path(__file__).resolve().with_name("avcapture18.c")
        with tempfile.TemporaryDirectory(prefix="zizhan-avcap-test-") as td:
            exe = Path(td) / "avcapture18"
            subprocess.run(
                [cc, "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", str(src), "-o", str(exe)],
                check=True,
            )
            out = subprocess.check_output([str(exe)], text=True)
            self.assertIn("PASS avcapture18", out)


if __name__ == "__main__":
    unittest.main()
