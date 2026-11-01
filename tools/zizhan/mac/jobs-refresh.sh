#!/bin/bash
# Mac: drop launchd jobs from the prepared OS image -- the service cache /System/Library/xpc/launchd.plist (build-ipados18.py jobs())
# and the job's file in /System/Library/LaunchDaemons -- recompute the UFS layout and rewrite only the OS window on the phone (in
# Android), as ui-refresh.sh does for the UI adapter. build-ipados18.py's EXCLUDED18 keeps the same jobs out of a full rebuild.
# Kept under TAG: prepared/pong-ipados18-system.TAG.cdr (APFS clone), ufs-metal/ufs-layout.TAG.json
# usage: jobs-refresh.sh TAG LABEL...   (11-01: com.apple.fairplayd.A2, docs/progress/2026-11-01-battery.md section 8)
set -u
cd ${ZIZHAN_SRC:-$HOME/zizhan} || exit 1
TAG=$1; shift
P=work/userland-22H374/prepared; U=work/userland-22H374/ufs-metal; W=work/gpu
export PATH=/opt/homebrew/bin:$PATH
t0=$(date +%s); st() { echo "$(date +%T) +$(( $(date +%s) - t0 ))s $*"; }

st "OS image (kept as pong-ipados18-system.$TAG.cdr)"
cp -c $P/pong-ipados18-system.cdr $P/pong-ipados18-system.$TAG.cdr || exit 1
M=$P/system-mount
dev=$(sudo -n hdiutil attach -readwrite -nobrowse -noverify -owners on -mountpoint $M $P/pong-ipados18-system.cdr | awk 'NR==1{print $1}')
C=$M/System/Library/xpc/launchd.plist
[ -f $C ] || { st "MOUNT_FAIL (no xpc/launchd.plist)"; sudo -n hdiutil detach $dev; exit 1; }
sudo -n python3 - $C "$@" <<'PY' || { st EDIT_FAIL; sudo -n hdiutil detach $dev; exit 1; }
import plistlib, sys, os
path, labels = sys.argv[1], set(sys.argv[2:])
raw = open(path, 'rb').read()
fmt = plistlib.FMT_BINARY if raw.startswith(b'bplist') else plistlib.FMT_XML
cache = plistlib.loads(raw)
jobs = cache['LaunchDaemons']
gone = [k for k, j in jobs.items() if j.get('Label') in labels]
for k in gone:
    del jobs[k]
    f = os.path.join(os.path.dirname(os.path.dirname(path)), 'LaunchDaemons', os.path.basename(k))
    if os.path.exists(f): os.unlink(f)
    print(f'   dropped {k}')
missing = labels - {k.rsplit("/", 1)[-1][:-6] for k in gone}
if missing: print(f'   not in the cache (already dropped?): {" ".join(sorted(missing))}')
st = os.stat(path)
open(path, 'wb').write(plistlib.dumps(cache, fmt=fmt))
os.chown(path, st.st_uid, st.st_gid); os.chmod(path, st.st_mode & 0o7777)
PY
sudo -n hdiutil detach $dev || { st DETACH_FAIL; exit 1; }

st "layout (hashing the three images)"
cp $U/ufs-layout.json $U/ufs-layout.$TAG.json
venv/bin/python UniXNU-zizhan/scripts/prepare-ipados18-ufs-layout.py --userland $P --systemos $P/pong-ipados18-systemos.cdr \
    --output-dir $U --volume-layout $P/volume-layout.json > $W/logs/layout-$TAG.log 2>&1 || { st LAYOUT_FAIL; tail -20 $W/logs/layout-$TAG.log; exit 1; }

st "install --only OS (adb, phone in Android)"
python3 install_pinned.py --layout $U/ufs-layout.json --only OS > $W/logs/install-$TAG.log 2>&1 || { st INSTALL_FAIL; tail -20 $W/logs/install-$TAG.log; exit 1; }
tail -3 $W/logs/install-$TAG.log | sed 's/^/   /'
st JOBS_REFRESH_DONE
