#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Test ShengBattmgr codec/dual-cell snapshot and audit PongBattery non-fabrication rules."""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
batt_cpp_raw = (ROOT / "drivers/PongPower/PongBattery.cpp").read_text()
batt_cpp_code = "\n".join(line.split("//", 1)[0] for line in batt_cpp_raw.splitlines())

# Non-fabrication policy (docs/research/2026-11-17-battery-health-inputs.md §8):
for forbidden in ("Battery Service State", "Battery Service Flags", "ageAwareMitigationState",
                  "UPOCount", "CurrentWRa", "AppleAuthCPRelay"):
    assert f'"{forbidden}"' not in batt_cpp_code, f"PongBattery.cpp must not fabricate {forbidden}"

for required in ("AppleRawBatteryVoltage", "PermanentFailureStatus", "ZIZHAN_BATT_DATA_KV:",
                 "ZIZHAN_BATT_HEALTH_DATA:", "zizhan_batt_board"):
    assert required in batt_cpp_raw, f"PongBattery.cpp missing expected symbol/key {required}"

cc = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang")
if not cc:
    print("sheng-battmgr.py: policy PASS; C compile SKIP (no cc)")
    sys.exit(0)

with tempfile.TemporaryDirectory(prefix="zizhan-battmgr-") as tmp:
    bin_path = Path(tmp) / "sheng-battmgr"
    subprocess.run([
        cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
        "-I" + str(ROOT / "drivers/PongPower"),
        str(ROOT / "drivers/PongPower/ShengBattmgr.c"),
        str(ROOT / "tools/zizhan/tests/sheng-battmgr.c"),
        "-o", str(bin_path),
    ], check=True, timeout=30)
    subprocess.run([str(bin_path)], check=True, timeout=10)
