#!/bin/bash
# Mac: unattended RAM test of an M3 A' boot image (fastboot boot only; no partition is written).
#   m3-ramtest.sh IMAGE OUTDIR
# Why (2026-11-22): hand-written fastboot loops lost ~1 h to the known USB re-attach failure after
# `adb reboot bootloader` (macOS: "setAddress completed with result code 4" -> "failed to address device,
# disabling port"). This script uses usbkick.sh (VBUS power cycle, docs/progress/2026-10-31-frame-timeline.md §8)
# instead of asking for a replug, restarts the adb server around the transition, and collects the verdict:
#   - logfs:\M3DIRECT.TXT (AndroidDirect: record, claims, memory map, fail reason or stage=jump)
#   - ABL boot reason of the boot that followed (UefiLog*.txt), Android "Kernel code" address
#     (stock flash boot 0xa8000000; a direct entry runs the Image from the A' kernel field)
#   - a new dropbox SYSTEM_RECOVERY_LOG (the direct boot asked for recovery)
# Exit: 0 verdict written; 2 precondition; 3 fastboot never appeared; 4 Android never came back (needs a human).
set -u
export PATH=/opt/homebrew/bin:$PATH
IMG=${1:?image}; OUT=${2:?outdir}
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
KICK="$HERE/usbkick.sh"
[ -f "$IMG" ] || { echo "no image: $IMG"; exit 2; }
mkdir -p "$OUT"
LOG="$OUT/ramtest.log"
ts() { date '+%H:%M:%S'; }
say() { echo "$(ts) $*" | tee -a "$LOG"; }
su_sh() { adb shell "su -c \"$1\""; }

adb_state() { adb devices 2>/dev/null | awk 'NR==2 {print $2}'; }
wait_fastboot() {   # $1 seconds
  local n=$(( $1 / 2 ))
  for _ in $(seq 1 "$n"); do fastboot devices 2>/dev/null | grep -q fastboot && return 0; sleep 2; done
  return 1
}
wait_adb() {        # $1 seconds; prints the last non-empty state
  local n=$(( $1 / 2 )) s=""
  for _ in $(seq 1 "$n"); do s=$(adb_state); [ "$s" = device ] && { echo device; return 0; }; sleep 2; done
  echo "${s:-none}"; return 1
}
# Reboot into fastboot (usbkick when the attach fails), boot $1, wait for Android. Returns 0 when adb is back.
boot_image() {
  local ok=0 slot state
  say "adb reboot bootloader"
  adb reboot bootloader
  sleep 4
  adb kill-server >/dev/null 2>&1
  for try in 1 2 3; do
    if wait_fastboot 30; then ok=1; break; fi
    say "fastboot not visible (try $try): usbkick"
    "$KICK" kick | tee -a "$LOG"
  done
  [ $ok = 1 ] || { say "fastboot never appeared: needs a replug"; return 3; }
  slot=$(fastboot getvar current-slot 2>&1 | awk '/current-slot:/ {print $2}')
  [ "$slot" = b ] || { say "fastboot slot is '$slot', not b: stopping (fastboot reboot)"; fastboot reboot >/dev/null 2>&1; return 2; }
  [ -f "$1" ] || { say "no image $1: fastboot reboot"; fastboot reboot >/dev/null 2>&1; return 2; }
  say "fastboot boot $(basename "$1")"
  fastboot boot "$1" 2>&1 | tail -1 | tee -a "$LOG"
  t0=$(date +%s)
  state=$(wait_adb 200)
  if [ "$state" != device ]; then
    say "adb state '$state' after 200 s: usbkick"
    "$KICK" kick | tee -a "$LOG"
    adb kill-server >/dev/null 2>&1
    state=$(wait_adb 120)
  fi
  [ "$state" = device ] || { say "Android not back (adb '$state'): needs a human (recovery menu? choose Reboot, never wipe)"; return 4; }
  say "Android back after $(( $(date +%s) - t0 )) s"
  return 0
}
dump_logfs() {      # $1 local file
  su_sh "dd if=/dev/block/by-name/logfs of=/data/local/tmp/m3-logfs.img bs=1m 2>/dev/null; chmod 644 /data/local/tmp/m3-logfs.img" >/dev/null &&
    adb pull /data/local/tmp/m3-logfs.img "$1" >/dev/null
}

[ "$(adb_state)" = device ] || { say "precondition: no adb device"; exit 2; }
level=$(adb shell dumpsys battery | awk '/level:/ {print $2}')
temp=$(adb shell dumpsys battery | awk '/temperature:/ {print $2}')
say "battery ${level}% temp $((temp / 10)).$((temp % 10)) C"
if [ "${level:-0}" -lt "${GATE_L:-35}" ] || [ "${temp:-999}" -gt "${GATE_T:-380}" ]; then say "precondition: gate"; exit 2; fi
[ "$(adb shell getprop ro.boot.slot_suffix | tr -d '\r')" = _b ] || { say "precondition: not slot b"; exit 2; }
"$KICK" remember | tee -a "$LOG"
dump_logfs "$OUT/logfs.pre" || { say "precondition: logfs dump failed"; exit 2; }
before=$(su_sh "ls /data/system/dropbox/ | grep SYSTEM_RECOVERY_LOG | tail -1" | tr -d '\r')

boot_image "$IMG"; rc=$?
if [ $rc = 4 ]; then
  # A direct boot that ends in MIUI recovery needs a human to pick Reboot; then collect as usual.
  say "waiting up to 15 min for Android after a human picks Reboot"
  [ "$(wait_adb 900)" = device ] || exit 4
elif [ $rc != 0 ]; then
  exit $rc
fi
sleep 5

dump_logfs "$OUT/logfs.post" || say "logfs dump failed"
MTOOLS_SKIP_CHECK=1 mtype -i "$OUT/logfs.post" ::/M3DIRECT.TXT > "$OUT/M3DIRECT.TXT" 2>/dev/null || rm -f "$OUT/M3DIRECT.TXT"
for f in 0 1 2 3 4; do MTOOLS_SKIP_CHECK=1 mtype -i "$OUT/logfs.post" ::/UefiLog$f.txt > "$OUT/UefiLog$f.txt" 2>/dev/null; done
su_sh "grep -i kernel /proc/iomem; cat /proc/uptime" > "$OUT/android.txt" 2>&1
after=$(su_sh "ls /data/system/dropbox/ | grep SYSTEM_RECOVERY_LOG | tail -1" | tr -d '\r')
if [ -n "$after" ] && [ "$after" != "$before" ]; then
  su_sh "cat /data/system/dropbox/$after" > "$OUT/recovery-log.gz" && gzip -dcf "$OUT/recovery-log.gz" > "$OUT/recovery-log.txt" 2>/dev/null
fi
python3 - "$OUT" <<'EOF' | tee -a "$LOG"
import re, sys
from pathlib import Path
out = Path(sys.argv[1])
m3 = out / "M3DIRECT.TXT"
lines = m3.read_text(errors="replace").splitlines() if m3.exists() else []
stage = next((l for l in reversed(lines) if l.startswith("stage=")), "no M3DIRECT.TXT (AndroidDirect did not run)")
why = [l for l in lines if l.startswith("zizhan:") and "Android (direct)" not in l and "entering" not in l]
code = re.search(r"([0-9a-f]+)-[0-9a-f]+ : Kernel code", (out / "android.txt").read_text(errors="replace"))
reasons = []
for f in sorted(out.glob("UefiLog*.txt")):
    t = f.read_text(errors="replace")
    m = re.search(r"KeyPress:\d+, BootReason:(\d+)", t)
    if m:
        reasons.append("%s BootReason %s%s" % (f.stem, m[1], " recovery" if "Recovery Mode" in t else ""))
rec = out / "recovery-log.txt"
cmd = re.search(r'Command: (.*)', rec.read_text(errors="replace")) if rec.exists() else None
print("verdict: %s %s" % (stage, " ".join(why)))
print("android kernel code: %s" % (code[1] if code else "?"))
print("recovery: %s" % (cmd[1] if cmd else "none"))
print("abl: " + "; ".join(reasons))
EOF
# The direct kernel ran (stage=jump) but Android is the stock flash boot again: AndroidDirect moved that
# kernel's ramoops to 0x85200000, which RamoopsDump saves to logfs:\XNULOG.TXT (out/ramoops-dump.img).
DUMP_IMG=${M3_DUMP_IMG:-${ZIZHAN_SRC:-$HOME/zizhan}/out/ramoops-dump.img}
if grep -q '^stage=jump' "$OUT/M3DIRECT.TXT" 2>/dev/null && grep -q '^ *a8000000-' "$OUT/android.txt" && [ -f "$DUMP_IMG" ]; then
  say "saving the direct kernel's console with $(basename "$DUMP_IMG")"
  if boot_image "$DUMP_IMG"; then
    sleep 5
    dump_logfs "$OUT/logfs.dump" &&
      MTOOLS_SKIP_CHECK=1 mtype -i "$OUT/logfs.dump" ::/XNULOG.TXT > "$OUT/linux-console.txt" 2>/dev/null
    say "direct kernel console: $(wc -c < "$OUT/linux-console.txt" 2>/dev/null || echo 0) bytes in linux-console.txt"
  fi
fi
exit 0
