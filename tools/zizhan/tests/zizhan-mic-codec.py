#!/usr/bin/env python3
"""Host ASan/UBSan test for SM8475 WCD9385 + SWRM v1.7 + VA/TX macro mic state machine."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def main():
    with tempfile.TemporaryDirectory(prefix="zizhan-mic-codec-") as tmp:
        binary = Path(tmp) / "zizhan-mic-codec"
        cc = os.environ.get("CC", "cc")
        subprocess.run(
            [
                cc,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-I" + str(ROOT / "drivers/PongPower"),
                str(ROOT / "tools/zizhan/tests/zizhan-mic-codec.c"),
                "-o",
                str(binary),
            ],
            check=True,
            timeout=30,
        )
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    main()
