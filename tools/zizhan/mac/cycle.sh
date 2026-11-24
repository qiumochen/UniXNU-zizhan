#!/bin/bash
# Mac: one unattended bring-up round.
#   1. fastboot-boot IMG (adb reboot bootloader first when the phone is in Android)
#   2. wait for the kernel's pong_autoreset watchdog to bring the phone back to fastboot
#   3. fastboot-boot RamoopsDump, which saves the ramoops console to logfs:\XNULOG.TXT and resets into Android
#   4. read logfs over adb and save the log to logs/RUN.txt
# usage: cycle.sh IMG RUN [MAX_WAIT_SECONDS]
# When the phone stays invisible on USB (the bootloader's attach failed, usbkick.sh), the waits in 2 and 3 power-cycle its port.
set -uo pipefail
export PATH=/opt/homebrew/bin:$PATH MTOOLS_SKIP_CHECK=1
S=${ZIZHAN_SRC:-$HOME/zizhan}
if [ "$#" -lt 2 ]; then echo "usage: $0 IMG RUN [MAX_WAIT_SECONDS]" >&2; exit 2; fi
IMG=$1; RUN=$2; WAIT=${3:-600}
case "$RUN" in ''|*[!a-zA-Z0-9_-]*) echo 'run must contain only letters, digits, _ or -' >&2; exit 2;; esac
L=${ZIZHAN_SRC:-$HOME/zizhan}/logs; mkdir -p $L
KLOG_MARKER="$L/$RUN.klog-armed"
rm -f "$KLOG_MARKER"
ts() { date '+%H:%M:%S'; }
in_fastboot() { bash "$S/fastboot-visible.sh"; }
in_adb() { bash "$S/adb-visible.sh"; }

# Record the current battery temperature for correlation. APP-01 unattended
# rounds do not wait on a thermal threshold; each round remains time-bounded.
battery_temp() { adb shell dumpsys battery 2>/dev/null | awk '/temperature/{print $2; exit}'; }
if in_adb; then
  t=$(battery_temp)
  echo "$(ts) battery ${t:-?} (tenths C); temperature gate disabled"
fi
# klog (klog.py, boot-arg pong_klog_4k added by ci_boot_ufs.sh): reset the pinned file's header so only this round's copy counts.
KLOG=0
if in_adb && [ -f $S/klog.json ]; then python3 $S/klog.py arm && KLOG=1 || echo "$(ts) klog arm failed; ramoops only"; fi
bash $S/boot.sh "$IMG" "第 $RUN 轮已自动启动，全程不需要操作；屏幕有变化可以随手拍一张。" || exit 2
if [ "$KLOG" = 1 ]; then printf '%s\n' "$IMG" > "$KLOG_MARKER"; fi
echo "$(ts) booted $IMG; waiting up to ${WAIT}s for the autoreset into fastboot"
sleep 20
# The supervisor passes WAIT = pong_autoreset + 150, so XNU itself resets into fastboot at about WAIT - 150 s. Do not kick the port
# before then: a kick is a USB detach/attach, which iOS answers by lighting the screen (it ended every POWER standby window early)
# and which turns the battery readings into charge transitions.
# ZIZHAN_USB_OFF_AT=<s> (POWER measurements): switch the phone's port off once, that many seconds into the wait, so the battery alone
# powers the phone (no charging, no attach events); the kicks after the autoreset power it again.
start=$(date +%s); nextkick=$(( WAIT > 270 ? WAIT - 150 : 120 )); usboff=0
until in_fastboot; do
  if in_adb; then echo "$(ts) phone came back in Android: the ramoops console is lost"; exit 3; fi
  el=$(( $(date +%s) - start ))
  [ $el -ge $WAIT ] && { echo "$(ts) no fastboot after ${WAIT}s"; exit 4; }
  # After the autoreset the phone is in fastboot whether or not the Mac sees it, so from then on kick the port every 45 s.
  if [ -n "${ZIZHAN_USB_OFF_AT:-}" ] && [ $usboff = 0 ] && [ $el -ge "$ZIZHAN_USB_OFF_AT" ] && [ $el -lt $nextkick ]; then
    bash $S/usbkick.sh off; usboff=1
  fi
  if [ $el -ge $nextkick ]; then bash $S/usbkick.sh kick; nextkick=$(( el + 45 )); fi
  sleep 3
done
if [ "$KLOG" = 1 ] && [ -n "${ZIZHAN_ANDROID_RETURN_IMG:-}" ] && [ -f "$ZIZHAN_ANDROID_RETURN_IMG" ]; then
  echo "$(ts) fastboot is back after $(( $(date +%s) - start + 20 ))s; RAM-booting Android ($ZIZHAN_ANDROID_RETURN_IMG)"
  bash "$S/fastboot-boot.sh" "$ZIZHAN_ANDROID_RETURN_IMG" 2>&1 | tail -1 || exit 7
  SKIP_RAMOOPS_LOGFS=1
else
  echo "$(ts) fastboot is back after $(( $(date +%s) - start + 20 ))s; saving the ramoops to logfs"
  bash "$S/fastboot-boot.sh" "$S/out/ramoops-dump.img" 2>&1 | tail -1 || exit 7
  SKIP_RAMOOPS_LOGFS=0
fi
start=$(date +%s); nextkick=300; kicks=0   # M3 boot_b: ~65 s normal, up to 4.5 min on ABL retries; cap at 2 spaced kicks so repeated 60 s VBUS cycles do not disturb ABL or enter off-mode charging
until in_adb; do
  el=$(( $(date +%s) - start ))
  [ $el -ge 900 ] && { echo "$(ts) Android did not come back"; exit 5; }
  if [ "$kicks" -lt 2 ] && [ $el -ge $nextkick ]; then bash $S/usbkick.sh kick; kicks=$(( kicks + 1 )); nextkick=$(( el + 300 )); fi
  sleep 3
done
sleep 5
bash $S/usbkick.sh remember >/dev/null 2>&1
LOGFS=/tmp/logfs-$RUN.img
bash "$S/pull-logfs.sh" "$LOGFS" || exit $?
# DarwinBoot24 appends every refused hand-off (and its retries) to BOOTFAIL.TXT; keep it with the round and clear it.
if mtype -i "$LOGFS" ::BOOTFAIL.TXT > $L/$RUN.bootfail.txt 2>/dev/null; then
  echo "$(ts) DarwinBoot24 refused the boot (logs/$RUN.bootfail.txt):"; sed 's/^/  /' $L/$RUN.bootfail.txt
  adb shell -T "su -c 'rm -f /dev/logfs/BOOTFAIL.TXT /dev/logfs/bootfail.txt; sync'"   # Android mounts logfs at /dev/logfs
else rm -f $L/$RUN.bootfail.txt; fi
if [ "${SKIP_RAMOOPS_LOGFS:-0}" = 1 ]; then
  python3 $S/klog.py read $L/$RUN.klog.txt || { echo "$(ts) no fresh UFS klog after direct Android return"; exit 6; }
  cp $L/$RUN.klog.txt $L/$RUN.txt; echo "$(ts) using the klog copy (direct Android RAM return)"
else
  mtype -i "$LOGFS" ::XNULOG.TXT > $L/$RUN.txt 2>/dev/null || { echo "$(ts) no XNULOG.TXT in logfs"; mdir -i "$LOGFS" ::; exit 6; }
  # The kernel's UFS copy of the ramoops zones has no reset bit flips: when it was written this round it becomes RUN.txt.
  if [ $KLOG = 1 ] && python3 $S/klog.py read $L/$RUN.klog.txt; then
    mv $L/$RUN.txt $L/$RUN.ramoops.txt; cp $L/$RUN.klog.txt $L/$RUN.txt; echo "$(ts) using the klog copy (DRAM copy: $RUN.ramoops.txt)"
  fi
fi
rm -f "$KLOG_MARKER"
bash "$S/UniXNU-zizhan/tools/zizhan/mac/decode-shots.sh" "$RUN" || echo "$(ts) screenshot decoding failed; inspect $L/$RUN.txt"
rm -f "$L/$RUN.wifi-summary.txt"
python3 "$S/UniXNU-zizhan/tools/zizhan/mac/wifi-stage.py" "$L/$RUN.txt" > "$L/$RUN.wifi-summary.txt" || { rm -f "$L/$RUN.wifi-summary.txt"; echo "$(ts) Wi-Fi summary failed; inspect $L/$RUN.txt"; }
# ADB and logfs can appear before Android framework startup completes (g396).
# Preserve fresh logs first, then require a bounded target/completion witness.
python3 "$S/UniXNU-zizhan/tools/zizhan/mac/wait-android.py" \
  --timeout "${ANDROID_RETURN_TIMEOUT:-120}" --output "$L/$RUN.android-return.json" || exit 8
echo "$(ts) saved $L/$RUN.txt ($(wc -c < $L/$RUN.txt) bytes); battery $(battery_temp) (tenths C) back in Android"
[ -z "${ZIZHAN_ASK_FILE:-}" ] || printf '%s\n' "第 $RUN 轮结束，手机已回到安卓，日志已自动取回。" > "$ZIZHAN_ASK_FILE"
