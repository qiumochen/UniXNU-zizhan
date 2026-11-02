#!/bin/bash
# Copy this round's logfs image. Remove previous and partial local copies on
# every failure, including an adb command that stops responding after USB loss.
set -u
python3 - "$1" "${ADB_BIN:-adb}" <<'PY'
import os
from pathlib import Path
import subprocess
import sys

out = Path(sys.argv[1])
adb = sys.argv[2]
out.unlink(missing_ok=True)

def run(args, timeout):
    try:
        result = subprocess.run(
            [adb, *args], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            timeout=timeout,
        )
        return result.returncode == 0
    except (subprocess.TimeoutExpired, OSError):
        return False

export = "su -c 'rm -f /data/local/tmp/logfs.img; dd if=/dev/block/by-name/logfs of=/data/local/tmp/logfs.img bs=1048576 2>/dev/null'"
if not run(["shell", "-T", export], max(1, int(os.environ.get("LOGFS_EXPORT_TIMEOUT", "60")))):
    print("logfs export failed or timed out", file=sys.stderr)
    sys.exit(8)
if not run(["pull", "/data/local/tmp/logfs.img", str(out)],
           max(1, int(os.environ.get("LOGFS_PULL_TIMEOUT", "120")))):
    out.unlink(missing_ok=True)
    print("logfs pull failed or timed out", file=sys.stderr)
    sys.exit(9)
if not out.is_file() or out.stat().st_size == 0:
    out.unlink(missing_ok=True)
    print("logfs pull returned an empty image", file=sys.stderr)
    sys.exit(9)
PY
