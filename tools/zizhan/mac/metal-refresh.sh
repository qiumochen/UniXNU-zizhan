#!/bin/bash
# Mac: rebuild the Metal bridge (NativeBridgeMetalFrontend.mm + metal2vulkan) and the plugin (PongMetalPlugin.mm) from
# UniXNU-zizhan/drivers/PongMetal, stage the bundle in the prepared OS image (refresh_metal), recompute the UFS layout and rewrite
# only the OS window of the pinned root image on the phone (in Android). Everything it replaces is kept under TAG:
#   work/gpu/plugin.TAG, prepared/pong-ipados18-system.TAG.cdr (APFS clone), ufs-metal/ufs-layout.TAG.json
# usage: metal-refresh.sh TAG      (docs/progress/2026-10-31-frame-timeline.md)
set -u
cd ${ZIZHAN_SRC:-$HOME/zizhan} || exit 1
TAG=$1
W=work/gpu; P=work/userland-22H374/prepared; U=work/userland-22H374/ufs-metal
export PATH=$HOME/.cargo/bin:/opt/homebrew/bin:$PATH DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
t0=$(date +%s); st() { echo "$(date +%T) +$(( $(date +%s) - t0 ))s $*"; }

st "bridge (cargo + frontend)"
bash $W/run-bridge.sh > $W/logs/bridge-$TAG.log 2>&1
grep -q "BRIDGE-EXIT=0" $W/logs/bridge-$TAG.log || { st BRIDGE_FAIL; grep -m10 -E "error|Error" $W/logs/bridge-$TAG.log; tail -15 $W/logs/bridge-$TAG.log; exit 1; }

st plugin
rm -rf $W/plugin.$TAG && cp -R $W/plugin $W/plugin.$TAG
(cd UniXNU-zizhan && ../venv/bin/python scripts/build-pong-metal-plugin.py --bridge-directory ../$W/bridge --guest-build 22H374 \
    --turnip-library ../$W/turnip/src/freedreno/vulkan/libvulkan_freedreno.dylib --output-dir ../$W/plugin --clients '*') \
    > $W/logs/plugin-$TAG.log 2>&1 || { st PLUGIN_FAIL; tail -30 $W/logs/plugin-$TAG.log; exit 1; }
ls -la $W/plugin/PongAdrenoMetal.bundle/ | sed 's/^/   /'

st "refresh_metal (OS image kept as pong-ipados18-system.$TAG.cdr)"
cp -c $P/pong-ipados18-system.cdr $P/pong-ipados18-system.$TAG.cdr || exit 1
sudo -n venv/bin/python UniXNU-zizhan/userland/build-ipados18.py refresh_metal --out $P --runtime $W/plugin > $W/logs/refresh-$TAG.log 2>&1 \
    || { st REFRESH_FAIL; tail -30 $W/logs/refresh-$TAG.log; exit 1; }
sudo -n chown -R "$(id -un)" $W/plugin/os-metal-update.json 2>/dev/null

st "layout (hashing the three images)"
cp $U/ufs-layout.json $U/ufs-layout.$TAG.json
venv/bin/python UniXNU-zizhan/scripts/prepare-ipados18-ufs-layout.py --userland $P --systemos $P/pong-ipados18-systemos.cdr \
    --output-dir $U --volume-layout $P/volume-layout.json > $W/logs/layout-$TAG.log 2>&1 || { st LAYOUT_FAIL; tail -20 $W/logs/layout-$TAG.log; exit 1; }
python3 - $U/ufs-layout.$TAG.json $U/ufs-layout.json <<'PY'
import json, sys
old, new = [json.load(open(p)) for p in sys.argv[1:]]
for a, b in zip(old['windows'], new['windows']):
    print(f"   {a['name']:9s} {'same' if a['sha256'] == b['sha256'] else 'CHANGED'} offset {a['offset']}->{b['offset']} size {a['size']}->{b['size']}")
PY

st "install --only OS (adb, phone in Android)"
python3 install_pinned.py --layout $U/ufs-layout.json --only OS > $W/logs/install-$TAG.log 2>&1 || { st INSTALL_FAIL; tail -20 $W/logs/install-$TAG.log; exit 1; }
tail -4 $W/logs/install-$TAG.log | sed 's/^/   /'
st METAL_REFRESH_DONE
