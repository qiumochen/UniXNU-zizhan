#!/bin/bash
# Mac: put the phone in fastboot (adb reboot bootloader if it is in Android) and RAM-boot an image.
# usage: boot.sh IMG ["ask text for the photo page"]
# The bootloader's first USB attach on this Mac always fails and about one in ten never retries (see usbkick.sh); after 20 s without
# fastboot the phone's port is power-cycled, up to 3 times, instead of giving up.
set -u
export PATH=/opt/homebrew/bin:$PATH
IMG=$1
ASK=${ZIZHAN_ASK_FILE:-}   # optional: file a phone-side photo page shows as the current request
S=${ZIZHAN_SRC:-$HOME/zizhan}
if ! fastboot devices | grep -q fastboot; then
  bash $S/usbkick.sh remember >/dev/null 2>&1
  adb reboot bootloader >/dev/null 2>&1
  for kick in 0 1 2 3; do
    for i in $(seq 1 20); do fastboot devices | grep -q fastboot && break 2; sleep 1; done
    [ $kick -lt 3 ] && bash $S/usbkick.sh kick
  done
fi
fastboot devices | grep -q fastboot || { echo "NO_FASTBOOT"; exit 2; }
[ $# -ge 2 ] && [ -n "$ASK" ] && printf '%s\n' "$2" > "$ASK"
fastboot boot "$IMG" 2>&1 | tail -2
date '+booted %H:%M:%S'
