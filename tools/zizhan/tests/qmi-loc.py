#!/usr/bin/env python3
"""Host ASan/UBSan test for pure-C ZizhanQmiLoc.h."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def main():
    cc = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "qmi-loc"
        subprocess.run(
            [
                cc, "-std=c11", "-O2", "-g",
                "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I" + str(ROOT / "drivers/PongPower"),
                str(ROOT / "tools/zizhan/tests/qmi-loc.c"),
                "-lm", "-o", str(exe),
            ],
            check=True,
        )
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
