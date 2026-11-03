#!/usr/bin/env python3
"""Replay Wi-Fi context/readback failures and WMI send-wait ownership cases.

Requires a C compiler and the locally provisioned Ath12kWifi shared headers.
Does not access a phone, firmware or network credentials.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
import pong_paths

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cc", default=os.environ.get("CC", "cc"))
    p.add_argument("--shared", type=Path, default=pong_paths.sheng_root() / "drivers/Ath12kWifi")
    args = p.parse_args()
    tests = ROOT / "tools/zizhan/tests"
    smmu = ROOT / "drivers/PongAdreno/ArmSmmu4K"
    with tempfile.TemporaryDirectory(prefix="zizhan-wifi-host-") as td:
        for name, inputs, include in [
            ("context", [tests / "wifi-context-readback.c", smmu / "ArmSmmu4KContext.c"], smmu),
            ("wmi-wait", [tests / "wifi-wmi-wait.c"], args.shared / "shim/include"),
        ]:
            binary = Path(td) / name
            subprocess.run([args.cc, "-std=gnu11", "-Wno-macro-redefined", "-Wno-unused-function",
                "-I", str(include), *map(str, inputs), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
if __name__ == "__main__":
    main()
