#!/bin/bash
# Mac: rebuild the UI adapter (/pong/libpong-ui18.dylib = userland/ui_compat18.c + settings_compat18.c + boot_snapshot18.c + touch.c, the
# compile command build-ipados18.py's apply used, prepared/ui-compile-command.json), put it in the prepared OS image, recompute the UFS
# layout and rewrite only the OS window on the phone (in Android), as metal-refresh.sh does for the Metal bundle.
# Kept under TAG: prepared/libpong-ui.TAG.dylib, prepared/pong-ipados18-system.TAG.cdr (APFS clone), ufs-metal/ufs-layout.TAG.json
# usage: ui-refresh.sh TAG
set -uo pipefail
if [ "$#" -ne 1 ]; then echo "usage: $0 TAG" >&2; exit 2; fi
TAG=$1
case "$TAG" in ''|*[!a-zA-Z0-9_-]*) echo 'tag must contain only letters, digits, _ or -' >&2; exit 2;; esac
SRC=${ZIZHAN_SRC:-$HOME/zizhan}
cd "$SRC" || exit 1
P=work/userland-22H374/prepared; U=work/userland-22H374/ufs-metal; W=work/gpu
export PATH=/opt/homebrew/bin:$PATH
t0=$(date +%s); st() { echo "$(date +%T) +$(( $(date +%s) - t0 ))s $*"; }
[ -f "$P/ui-compile-command.json" ] && [ -f "$P/libpong-ui.dylib" ] && [ -f "$P/pong-ipados18-system.cdr" ] && [ -f "$U/ufs-layout.json" ] || { echo 'prepared UI or layout missing' >&2; exit 1; }
[ ! -e "$P/libpong-ui.$TAG.dylib" ] && [ ! -e "$P/pong-ipados18-system.$TAG.cdr" ] && [ ! -e "$U/ufs-layout.$TAG.json" ] || { echo 'backup tag already exists' >&2; exit 1; }
bash "$SRC/UniXNU-zizhan/tools/zizhan/mac/adb-visible.sh" || { echo 'Android adb device required before editing the OS image' >&2; exit 1; }

st compile
cp "$P/libpong-ui.dylib" "$P/libpong-ui.$TAG.dylib" || exit 1
DEVELOPER_DIR=$(python3 -c "import json;c=json.load(open('$P/ui-compile-command.json'));s=c[c.index('-isysroot')+1];print(s.split('/Platforms/')[0])")
python3 - $P/ui-compile-command.json <<'PY' > /tmp/ui-compile.sh
import json, shlex, sys
print(' '.join(shlex.quote(x) for x in json.load(open(sys.argv[1]))))
PY
DEVELOPER_DIR=$DEVELOPER_DIR bash /tmp/ui-compile.sh > $W/logs/ui-$TAG.log 2>&1 || { st COMPILE_FAIL; tail -20 $W/logs/ui-$TAG.log; exit 1; }
codesign --force --sign - --pagesize 4096 $P/libpong-ui.dylib >> $W/logs/ui-$TAG.log 2>&1 || { st SIGN_FAIL; exit 1; }

st "OS image (kept as pong-ipados18-system.$TAG.cdr)"
cp -c "$P/pong-ipados18-system.cdr" "$P/pong-ipados18-system.$TAG.cdr" || exit 1
M=$(mktemp -d "$P/ui-mount.XXXXXX")
dev=
cleanup() {
  [ -z "$dev" ] || sudo -n hdiutil detach "$dev" || true
  rmdir "$M" 2>/dev/null || true
}
trap cleanup EXIT
dev=$(sudo -n hdiutil attach -readwrite -nobrowse -noverify -owners on -mountpoint "$M" "$P/pong-ipados18-system.cdr" | awk 'NR==1{print $1}') || exit 1
[ -n "$dev" ] && [ -f "$M/pong/libpong-ui18.dylib" ] || { st "MOUNT_FAIL (no /pong/libpong-ui18.dylib)"; exit 1; }
sudo -n cp "$P/libpong-ui.dylib" "$M/pong/libpong-ui18.dylib" && sudo -n chown 0:0 "$M/pong/libpong-ui18.dylib" && sudo -n chmod 755 "$M/pong/libpong-ui18.dylib" || { st COPY_FAIL; exit 1; }
ls -la "$M/pong/libpong-ui18.dylib" | sed 's/^/   /'
# Photos ignores the inherited DYLD_INSERT_LIBRARIES environment in this runtime.
# A direct load command is needed so its PXCreateContext compatibility hook runs.
photos=$M/Applications/Photos.app/Photos
if [ -f "$photos" ]; then
  sudo -n python3 "$SRC/UniXNU-zizhan/tools/zizhan/mac/add-load-command.py" "$photos" /pong/libpong-ui18.dylib || \
    { st "PHOTOS_LOAD_COMMAND_FAIL"; exit 1; }
fi
sudo -n hdiutil detach "$dev" || { st DETACH_FAIL; exit 1; }
dev=

st "layout (hashing the three images)"
cp "$U/ufs-layout.json" "$U/ufs-layout.$TAG.json" || exit 1
venv/bin/python UniXNU-zizhan/scripts/prepare-ipados18-ufs-layout.py --userland $P --systemos $P/pong-ipados18-systemos.cdr \
    --output-dir $U --volume-layout $P/volume-layout.json > $W/logs/layout-$TAG.log 2>&1 || { st LAYOUT_FAIL; tail -20 $W/logs/layout-$TAG.log; exit 1; }

st "install --only OS (adb, phone in Android)"
python3 install_pinned.py --layout $U/ufs-layout.json --only OS > $W/logs/install-$TAG.log 2>&1 || { st INSTALL_FAIL; tail -20 $W/logs/install-$TAG.log; exit 1; }
tail -3 $W/logs/install-$TAG.log | sed 's/^/   /'
st UI_REFRESH_DONE
