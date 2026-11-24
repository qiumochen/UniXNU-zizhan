#!/usr/bin/env python3
"""Host tests for SENS-04 ALS auto-brightness curve, f32 decoder, and standby quiesce."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="zizhan-slpi-sensors-") as temp:
    binary = Path(temp) / "slpi-sensors"
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-I" + str(ROOT / "drivers/PongPower"),
        str(ROOT / "tools/zizhan/tests/slpi-sensors.cpp"), "-o", str(binary),
    ], check=True, timeout=30)
    subprocess.run([str(binary)], check=True, timeout=10)

slpi = (ROOT / "drivers/PongPower/ZizhanSlpi.cpp").read_text()
assert 'snsSuid("ambient_light")' in slpi
assert 'snsSuid("gyro")' in slpi
assert "standby quiesce streams=off" in slpi
print("slpi-sensors.py: PASS")
