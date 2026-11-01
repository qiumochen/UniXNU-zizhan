#!/bin/bash
# Mac: put the phone's own ADSP firmware (merged adsp.mbn, tools/mac/mdt2mbn.py from Android /vendor/firmware_mnt/image/adsp.*) at
# /pong/firmware/adsp.mbn in the prepared OS image, recompute the UFS layout and rewrite the OS window (phone in Android), as
# ui-refresh.sh does. The firmware never goes into git. Kept under TAG: prepared/pong-ipados18-system.TAG.cdr, ufs-metal/ufs-layout.TAG.json
# usage: fw-refresh.sh TAG MBN
set -u
cd ${ZIZHAN_SRC:-$HOME/zizhan} || exit 1
TAG=$1; MBN=$2
P=work/userland-22H374/prepared; U=work/userland-22H374/ufs-metal; W=work/gpu
export PATH=/opt/homebrew/bin:$PATH
t0=$(date +%s); st() { echo "$(date +%T) +$(( $(date +%s) - t0 ))s $*"; }
[ -f "$MBN" ] || { st "NO_FIRMWARE $MBN"; exit 1; }
st "OS image (kept as pong-ipados18-system.$TAG.cdr)"
cp -c $P/pong-ipados18-system.cdr $P/pong-ipados18-system.$TAG.cdr || exit 1
M=$P/system-mount
dev=$(sudo -n hdiutil attach -readwrite -nobrowse -noverify -owners on -mountpoint $M $P/pong-ipados18-system.cdr | awk 'NR==1{print $1}')
[ -d $M/pong ] || { st "MOUNT_FAIL (no /pong)"; sudo -n hdiutil detach $dev; exit 1; }
sudo -n mkdir -p $M/pong/firmware && sudo -n cp "$MBN" $M/pong/firmware/adsp.mbn && sudo -n chown -R 0:0 $M/pong/firmware && sudo -n chmod 644 $M/pong/firmware/adsp.mbn
ls -la $M/pong/firmware | sed 's/^/   /'
sudo -n hdiutil detach $dev || { st DETACH_FAIL; exit 1; }
st "layout (hashing the three images)"
cp $U/ufs-layout.json $U/ufs-layout.$TAG.json
venv/bin/python UniXNU-zizhan/scripts/prepare-ipados18-ufs-layout.py --userland $P --systemos $P/pong-ipados18-systemos.cdr \
    --output-dir $U --volume-layout $P/volume-layout.json > $W/logs/layout-$TAG.log 2>&1 || { st LAYOUT_FAIL; tail -20 $W/logs/layout-$TAG.log; exit 1; }
st "install --only OS (adb, phone in Android)"
python3 install_pinned.py --layout $U/ufs-layout.json --only OS > $W/logs/install-$TAG.log 2>&1 || { st INSTALL_FAIL; tail -20 $W/logs/install-$TAG.log; exit 1; }
tail -2 $W/logs/install-$TAG.log | sed 's/^/   /'
st FW_REFRESH_DONE
