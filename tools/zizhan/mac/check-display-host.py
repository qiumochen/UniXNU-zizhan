#!/usr/bin/env python3
"""Replay display command ownership without MMIO, firmware or a phone."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = parser.parse_args()
    subprocess.run([sys.executable, str(ROOT / "tools/zizhan/tests/brightness-curve.py"),
                    "--cc", args.cc], check=True)
    with tempfile.TemporaryDirectory(prefix="zizhan-display-host-") as td:
        binary = Path(td) / "dsi-lease"
        subprocess.run([args.cc, "-std=c11", "-pthread", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "tools/zizhan/tests/dsi-lease.c"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    main()
