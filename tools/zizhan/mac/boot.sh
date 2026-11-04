#!/bin/bash
# Mac: put the phone in fastboot (adb reboot bootloader if it is in Android) and RAM-boot an image.
# usage: boot.sh IMG ["ask text for the photo page"]
# The bootloader's first USB attach on this Mac always fails and about one in ten never retries (see usbkick.sh); after 20 s without
# fastboot the phone's port is power-cycled, up to 3 times, instead of giving up.
set -uo pipefail
export PATH=/opt/homebrew/bin:$PATH
IMG=$1
[ -f "$IMG" ] || { echo "NO_IMAGE"; exit 2; }
ASK=${ZIZHAN_ASK_FILE:-}   # optional: file a phone-side photo page shows as the current request
S=${ZIZHAN_SRC:-$HOME/zizhan}
in_fastboot() { bash "$S/fastboot-visible.sh"; }
if ! in_fastboot; then
  bash "$S/adb-visible.sh" || { echo "NO_DEVICE"; exit 2; }
  bash $S/usbkick.sh remember >/dev/null 2>&1
  python3 - <<'PY' || { echo "ADB_REBOOT_FAILED"; exit 2; }
import os
import subprocess
import sys

try:
    result = subprocess.run(
        [os.environ.get("ADB_BIN", "adb"), "reboot", "bootloader"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        timeout=max(1, int(os.environ.get("ADB_REBOOT_TIMEOUT", "30"))),
    )
except (subprocess.TimeoutExpired, OSError):
    sys.exit(1)
sys.exit(result.returncode)
PY
  for kick in 0 1 2 3; do
    deadline=$((SECONDS + 20))
    while [ "$SECONDS" -lt "$deadline" ]; do in_fastboot && break 2; sleep 1; done
    [ $kick -lt 3 ] && bash $S/usbkick.sh kick
  done
fi
in_fastboot || { echo "NO_FASTBOOT"; exit 2; }
[ $# -ge 2 ] && [ -n "$ASK" ] && printf '%s\n' "$2" > "$ASK"
bash "$S/fastboot-boot.sh" "$IMG" || exit 3
date '+booted %H:%M:%S'
