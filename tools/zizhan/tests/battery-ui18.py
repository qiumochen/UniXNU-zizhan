#!/usr/bin/env python3
"""Host ASan/UBSan test for Stage 5 real ADSP battery health telemetry formatting."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def main():
    with tempfile.TemporaryDirectory(prefix="zizhan-battery-ui18-") as tmp:
        binary = Path(tmp) / "battery-ui18"
        cc = os.environ.get("CC", "cc")
        subprocess.run(
            [
                cc,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-I" + str(ROOT / "userland"),
                str(ROOT / "tools/zizhan/tests/battery-ui18.c"),
                "-o",
                str(binary),
            ],
            check=True,
            timeout=30,
        )
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    main()
