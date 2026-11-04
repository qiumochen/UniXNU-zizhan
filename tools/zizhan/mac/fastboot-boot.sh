#!/bin/bash
# Boot a temporary image with a bounded transfer and preserve fastboot's status.
python3 - "$1" "$0" <<'PY'
import os
from pathlib import Path
import subprocess
import sys
import time

fastboot = [os.environ.get("FASTBOOT_BIN", "fastboot")]
if os.environ.get("ANDROID_SERIAL"):
    fastboot += ["-s", os.environ["ANDROID_SERIAL"]]
probe = Path(sys.argv[2]).resolve().with_name("fastboot-visible.sh")

def transfer():
    try:
        result = subprocess.run(fastboot + ["boot", sys.argv[1]],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180)
    except subprocess.TimeoutExpired as exc:
        if exc.stdout:
            sys.stdout.write(exc.stdout.decode(errors="replace") if isinstance(exc.stdout, bytes) else exc.stdout)
        print("fastboot boot timed out after 180 seconds", file=sys.stderr)
        sys.exit(124)
    except OSError as exc:
        print(f"fastboot boot failed: {exc}", file=sys.stderr)
        sys.exit(127)
    sys.stdout.write(result.stdout)
    sys.stdout.flush()
    return result

def ready():
    try:
        return (subprocess.run(["bash", str(probe)], capture_output=True, timeout=14).returncode == 0
                and subprocess.run(fastboot + ["getvar", "product"],
                    capture_output=True, timeout=6).returncode == 0)
    except (subprocess.TimeoutExpired, OSError):
        return False

result = transfer()
lines = result.stdout.splitlines()
# Never retry an ambiguous timeout or a failure after the boot command.
# The USB send error specifically precedes execution of the image.
if (result.returncode and not any(line.lstrip().startswith("Booting") for line in lines)
        and any(line.lstrip().startswith("Sending ") and "FAILED (usb_write failed" in line for line in lines)):
    print("fastboot: waiting for USB to settle before one transfer retry", file=sys.stderr)
    time.sleep(3)
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if ready():
            result = transfer()
            break
        time.sleep(1)
sys.exit(result.returncode)
PY
