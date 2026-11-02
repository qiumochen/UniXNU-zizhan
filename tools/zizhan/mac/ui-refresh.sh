#!/bin/bash
# Mac: rebuild the UI adapter (/pong/libpong-ui18.dylib = userland/ui_compat18.c + settings_compat18.c + boot_snapshot18.c + touch.c, the
# compile command build-ipados18.py's apply used, prepared/ui-compile-command.json), put it in the prepared OS image, recompute the UFS
# layout and rewrite only the OS window on the phone (in Android), as metal-refresh.sh does for the Metal bundle.
# Kept under TAG: prepared/libpong-ui.TAG.dylib, prepared/pong-ipados18-system.TAG.cdr (APFS clone), ufs-metal/ufs-layout.TAG.json
# usage: ui-refresh.sh TAG
set -u
cd ${ZIZHAN_SRC:-$HOME/zizhan} || exit 1
TAG=$1
P=work/userland-22H374/prepared; U=work/userland-22H374/ufs-metal; W=work/gpu
export PATH=/opt/homebrew/bin:$PATH
t0=$(date +%s); st() { echo "$(date +%T) +$(( $(date +%s) - t0 ))s $*"; }

st compile
cp $P/libpong-ui.dylib $P/libpong-ui.$TAG.dylib
DEVELOPER_DIR=$(python3 -c "import json;c=json.load(open('$P/ui-compile-command.json'));s=c[c.index('-isysroot')+1];print(s.split('/Platforms/')[0])")
python3 - $P/ui-compile-command.json <<'PY' > /tmp/ui-compile.sh
import json, shlex, sys
print(' '.join(shlex.quote(x) for x in json.load(open(sys.argv[1]))))
PY
DEVELOPER_DIR=$DEVELOPER_DIR bash /tmp/ui-compile.sh > $W/logs/ui-$TAG.log 2>&1 || { st COMPILE_FAIL; tail -20 $W/logs/ui-$TAG.log; exit 1; }
codesign --force --sign - --pagesize 4096 $P/libpong-ui.dylib >> $W/logs/ui-$TAG.log 2>&1 || { st SIGN_FAIL; exit 1; }

st "OS image (kept as pong-ipados18-system.$TAG.cdr)"
cp -c $P/pong-ipados18-system.cdr $P/pong-ipados18-system.$TAG.cdr || exit 1
M=$P/system-mount
dev=$(sudo -n hdiutil attach -readwrite -nobrowse -noverify -owners on -mountpoint $M $P/pong-ipados18-system.cdr | awk 'NR==1{print $1}')
[ -f $M/pong/libpong-ui18.dylib ] || { st "MOUNT_FAIL (no /pong/libpong-ui18.dylib)"; sudo -n hdiutil detach $dev; exit 1; }
sudo -n cp $P/libpong-ui.dylib $M/pong/libpong-ui18.dylib && sudo -n chown 0:0 $M/pong/libpong-ui18.dylib && sudo -n chmod 755 $M/pong/libpong-ui18.dylib
ls -la $M/pong/libpong-ui18.dylib | sed 's/^/   /'
# Photos ignores the inherited DYLD_INSERT_LIBRARIES environment in this runtime.
# A direct load command is needed so its PXCreateContext compatibility hook runs.
photos=$M/Applications/Photos.app/Photos
if [ -f "$photos" ]; then
  sudo -n python3 $SRC/UniXNU-zizhan/tools/zizhan/mac/add-load-command.py "$photos" /pong/libpong-ui18.dylib || \
    { st "PHOTOS_LOAD_COMMAND_FAIL"; sudo -n hdiutil detach $dev; exit 1; }
fi
sudo -n hdiutil detach $dev || { st DETACH_FAIL; exit 1; }

st "layout (hashing the three images)"
cp $U/ufs-layout.json $U/ufs-layout.$TAG.json
venv/bin/python UniXNU-zizhan/scripts/prepare-ipados18-ufs-layout.py --userland $P --systemos $P/pong-ipados18-systemos.cdr \
    --output-dir $U --volume-layout $P/volume-layout.json > $W/logs/layout-$TAG.log 2>&1 || { st LAYOUT_FAIL; tail -20 $W/logs/layout-$TAG.log; exit 1; }

st "install --only OS (adb, phone in Android)"
python3 install_pinned.py --layout $U/ufs-layout.json --only OS > $W/logs/install-$TAG.log 2>&1 || { st INSTALL_FAIL; tail -20 $W/logs/install-$TAG.log; exit 1; }
tail -3 $W/logs/install-$TAG.log | sed 's/^/   /'
st UI_REFRESH_DONE
