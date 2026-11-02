#!/bin/bash
# Return success only for an Android device in the "device" state. USB failures
# can leave platform-tools blocked, so this probe must be bounded.
python3 - <<'PY'
import os
import subprocess
import sys

try:
    result = subprocess.run(
        [os.environ.get("ADB_BIN", "adb"), "devices"],
        capture_output=True, text=True, timeout=6,
    )
except (subprocess.TimeoutExpired, OSError):
    sys.exit(1)
sys.exit(0 if result.returncode == 0 and any(
    len(parts) >= 2 and parts[1] == "device"
    for parts in (line.split() for line in result.stdout.splitlines())
) else 1)
PY
