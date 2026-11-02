#!/bin/bash
# Return success only when a fastboot device is visible. A missing USB device
# can make platform-tools' `fastboot devices` block indefinitely on this host.
python3 - <<'PY'
import subprocess
import sys

try:
    result = subprocess.run(
        ["fastboot", "devices"], capture_output=True, text=True, timeout=6
    )
except (subprocess.TimeoutExpired, OSError):
    sys.exit(1)
sys.exit(0 if any(line.rstrip().endswith("\tfastboot") for line in result.stdout.splitlines()) else 1)
PY
