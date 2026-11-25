#!/usr/bin/env python3
"""Host ASan/UBSan test for QCA6490 3 Mbps SE20 RCG planner, NVM fixup, H4 shim, and IBS framing."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def main():
    with tempfile.TemporaryDirectory(prefix="zizhan-bt-hci-shim-") as tmp:
        binary = Path(tmp) / "bt-hci-shim"
        cc = os.environ.get("CC", "cc")
        subprocess.run(
            [
                cc,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-I" + str(ROOT / "drivers/ArmDTPlatform"),
                str(ROOT / "tools/zizhan/tests/bt-hci-shim.c"),
                "-o",
                str(binary),
            ],
            check=True,
            timeout=30,
        )
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    main()
