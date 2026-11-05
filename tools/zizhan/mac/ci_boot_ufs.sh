#!/bin/bash
# Mac: Pong's full 22H374 package (prelink + UFS root windows + userland boot args) on the zizhan UEFI.
# The root image lives in a pinned file on Android /data (install_pinned.py prints the 4K block).
# usage: [CPUS=8] [PROFILE=zizhan_ipados18_gpu] ci_boot_ufs.sh NAME SHNGROOT_4K [extra kernel args...]
set -euo pipefail
SRC=${ZIZHAN_SRC:-$HOME/zizhan}
export PATH=$SRC/venv/bin:/opt/homebrew/bin:$PATH
source $SRC/ci-env.sh
export PONG_BOOT_TEMPLATE=$SRC/out/zizhan-uefi.img
cd $SRC/UniXNU-zizhan
NAME=${1:?name}; BLK=${2:?shngroot 4k block}; shift 2
# The device default includes GPU + Wi-Fi. Reduced CI builds remain explicit.
A=$(PROFILE=${PROFILE:-zizhan_ipados18_gpu} python3 -c "import json,os,pathlib;f=[p for p in pathlib.Path('out').rglob('PONG_FORMAT2_RESULT.json') if p.parent.name==os.environ['PROFILE']];assert len(f)==1,f;print(pathlib.Path(json.loads(f[0].read_text())['output'])/'kernel')")
OUT=$SRC/out/$NAME; rm -rf $OUT
# GPU/Wi-Fi startup follows the compiled kernel report. WIFI=0 is an explicit
# radio-off diagnostic override. Touch, audio and ADSP retain their separate
# board bring-up policy; ADSP=1/2 enables the battery path.
# Scaled machine timeouts as UniXNU-sheng's board.toml: the DEVELOPMENT kernel panics when interrupts stay masked over 500 us
# or a lock spins too long; under Gunyah on this SoC that trips on ordinary stalls (g93: 658 us in mk_timer_expire). x40 keeps
# the checks for real hangs. The interrupt-masked check still panicked at about 1.7 ms with its scale given (g96), so it only
# records (interrupt-masked-debug-mode=1, SCHED_HYGIENE_MODE_TRACE) instead of panicking.
# pong_klog_4k (klog.py setup): the kernel copies the ramoops zones to a pinned file before resetting; KLOG=0 leaves it out.
KLOGARG=$( [ "${KLOG:-1}" != 1 ] || python3 $SRC/klog.py arg )
ARGS=($KLOGARG pong_shngroot_4k=$BLK pong_touch=0 pong_audio=0 pong_adsp=${ADSP:-0} ml-timeout-lock-panic-scale=40 interrupt-masked-debug-mode=1)
if [ -n "${WIFI:-}" ]; then ARGS+=(pong_wifi="$WIFI"); fi
ARGS+=("$@")
# The kernel is built with E/P clusters (patches/xnu-ipados18/0008) and hangs early without cluster-type in the ADT (g97).
export PONG_ADT_CLUSTERS=${PONG_ADT_CLUSTERS:-1}
KA=(); for x in "${ARGS[@]}"; do KA+=(--kernel-arg "$x"); done
python3 scripts/build-ipados18-prelinked-boot.py --artifact-dir $A --output-dir $OUT --volume-layout $PONG_VOLUME_LAYOUT \
  --cpus ${CPUS:-8} "${KA[@]}" > $OUT.log 2>&1 || { tail -20 $OUT.log; exit 1; }
tail -2 $OUT.log
grep '^args' $OUT/stage/EFI/UniXNU/unixnu.toml
# DarwinBoot24 takes at most 1024 bytes of command line (BootArgs.h); a longer one fails the boot and leaves the phone at the
# UniXNU menu with no watchdog armed (g98: 1033 bytes).
LEN=$(grep '^args' $OUT/stage/EFI/UniXNU/unixnu.toml | sed 's/^args = "//; s/"$//' | wc -c | tr -d ' ')
echo "command line $LEN bytes"
# ALLOW_LONG_CMDLINE=1 skips the check, to test DarwinBoot24's refused-boot path (retries, logfs:\BOOTFAIL.TXT, back to fastboot).
# (LEN counts the newline; DarwinBoot24 refuses a line over 1023 characters, see BootOnce.)
[ "$LEN" -le 1020 ] || [ "${ALLOW_LONG_CMDLINE:-0}" = 1 ] || { echo "COMMAND LINE TOO LONG ($LEN bytes, DarwinBoot24 takes 1023 characters)"; exit 1; }
ls -la $OUT/pong-xnu-initial.img
