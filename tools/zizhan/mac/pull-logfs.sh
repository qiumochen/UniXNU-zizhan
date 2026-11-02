#!/bin/bash
# Copy this round's logfs image. Never leave a previous or partial local copy
# where cycle.sh could mistake it for evidence from the current boot.
set -u
OUT=$1
ADB=${ADB_BIN:-adb}
rm -f "$OUT"
"$ADB" shell -T "su -c 'rm -f /data/local/tmp/logfs.img; dd if=/dev/block/by-name/logfs of=/data/local/tmp/logfs.img bs=1048576 2>/dev/null'" || {
  echo "logfs export failed" >&2
  exit 8
}
"$ADB" pull /data/local/tmp/logfs.img "$OUT" >/dev/null || {
  rm -f "$OUT"
  echo "logfs pull failed" >&2
  exit 9
}
if [ ! -s "$OUT" ]; then
  rm -f "$OUT"
  echo "logfs pull returned an empty image" >&2
  exit 9
fi
