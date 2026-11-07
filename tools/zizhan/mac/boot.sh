#!/bin/bash
# Mac: put the phone in fastboot (adb reboot bootloader if it is in Android) and RAM-boot an image.
# usage: boot.sh IMG ["ask text for the photo page"]
set -uo pipefail
export PATH=/opt/homebrew/bin:$PATH
IMG=$1
[ -f "$IMG" ] || { echo "NO_IMAGE"; exit 2; }
ASK=${ZIZHAN_ASK_FILE:-}   # optional: file a phone-side photo page shows as the current request
S=${ZIZHAN_SRC:-$HOME/zizhan}
ATTACH="$S/UniXNU-zizhan/tools/zizhan/mac/usb-attach.py"
[ -f "$ATTACH" ] || ATTACH="$(dirname "$0")/usb-attach.py"
python3 "$ATTACH" --enter || { echo "NO_FASTBOOT"; exit 2; }
[ $# -ge 2 ] && [ -n "$ASK" ] && printf '%s\n' "$2" > "$ASK"
bash "$S/fastboot-boot.sh" "$IMG" || exit 3
date '+booted %H:%M:%S'
