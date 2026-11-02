#!/bin/bash
# Boot a temporary image with a bounded transfer and preserve fastboot's status.
python3 - "$1" <<'PY'
import os
import subprocess
import sys

try:
    result = subprocess.run(
        [os.environ.get("FASTBOOT_BIN", "fastboot"), "boot", sys.argv[1]],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180,
    )
except subprocess.TimeoutExpired as exc:
    if exc.stdout:
        sys.stdout.write(exc.stdout.decode(errors="replace") if isinstance(exc.stdout, bytes) else exc.stdout)
    print("fastboot boot timed out after 180 seconds", file=sys.stderr)
    sys.exit(124)
except OSError as exc:
    print(f"fastboot boot failed: {exc}", file=sys.stderr)
    sys.exit(127)
sys.stdout.write(result.stdout)
sys.exit(result.returncode)
PY
