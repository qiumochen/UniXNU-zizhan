#!/bin/bash
# Mac: drop launchd jobs from the prepared OS image -- the service cache /System/Library/xpc/launchd.plist (build-ipados18.py jobs())
# and the job's file in /System/Library/LaunchDaemons -- recompute the UFS layout and rewrite only the OS window on the phone (in
# Android), as ui-refresh.sh does for the UI adapter. build-ipados18.py's EXCLUDED18 keeps the same jobs out of a full rebuild.
# Kept under TAG: prepared/pong-ipados18-system.TAG.cdr (APFS clone), ufs-metal/ufs-layout.TAG.json
# usage: jobs-refresh.sh TAG LABEL...   (11-01: com.apple.fairplayd.A2, docs/progress/2026-11-01-battery.md section 8)
set -euo pipefail
if [ "$#" -lt 2 ]; then echo "usage: $0 TAG LABEL..." >&2; exit 2; fi
TAG=$1; shift
case "$TAG" in ''|*[!a-zA-Z0-9_-]*) echo 'tag must contain only letters, digits, _ or -' >&2; exit 2;; esac
SRC=${ZIZHAN_SRC:-$HOME/zizhan}
cd "$SRC"
P=work/userland-22H374/prepared; U=work/userland-22H374/ufs-metal; W=work/gpu
export PATH=/opt/homebrew/bin:$PATH
t0=$(date +%s); st() { echo "$(date +%T) +$(( $(date +%s) - t0 ))s $*"; }
current="$P/pong-ipados18-system.cdr"
backup="$P/pong-ipados18-system.$TAG.cdr"
[ -f "$current" ] && [ -f "$U/ufs-layout.json" ]
[ ! -e "$backup" ] && [ ! -e "$U/ufs-layout.$TAG.json" ] || { echo 'backup tag already exists' >&2; exit 1; }
bash "$SRC/UniXNU-zizhan/tools/zizhan/mac/adb-visible.sh" || { echo 'Android adb device required before editing the OS image' >&2; exit 1; }
mkdir -p "$W/logs"

st "OS image (kept as pong-ipados18-system.$TAG.cdr)"
cp -c "$current" "$backup"
cp "$U/ufs-layout.json" "$U/ufs-layout.$TAG.json"
M=$(mktemp -d "$P/jobs-refresh.XXXXXX")
dev=
cleanup() {
  [ -z "$dev" ] || sudo -n hdiutil detach "$dev" || true
  rmdir "$M" 2>/dev/null || true
}
trap cleanup EXIT
dev=$(sudo -n hdiutil attach -readwrite -nobrowse -noverify -owners on -mountpoint "$M" "$current" | awk 'NR==1{print $1}')
[ -n "$dev" ]
C="$M/System/Library/xpc/launchd.plist"
[ -f "$C" ] || { st "MOUNT_FAIL (no xpc/launchd.plist)"; exit 1; }
sudo -n python3 - "$C" "$@" <<'PY'
import plistlib, sys, os
path, labels = sys.argv[1], set(sys.argv[2:])
raw = open(path, 'rb').read()
fmt = plistlib.FMT_BINARY if raw.startswith(b'bplist') else plistlib.FMT_XML
cache = plistlib.loads(raw)
jobs = cache['LaunchDaemons']
gone = [k for k, j in jobs.items() if j.get('Label') in labels]
missing = labels - {jobs[k]['Label'] for k in gone}
if missing: raise SystemExit('labels absent from cache: ' + ', '.join(sorted(missing)))
for k in gone:
    relative = k.lstrip('/').split('/')
    if relative[:3] != ['System', 'Library', 'LaunchDaemons'] or len(relative) != 4 or not relative[-1].endswith('.plist'):
        raise SystemExit('unexpected job path: ' + k)
for k in gone:
    del jobs[k]
    f = os.path.join(os.path.dirname(os.path.dirname(path)), 'LaunchDaemons', os.path.basename(k))
    if os.path.exists(f): os.unlink(f)
    print(f'   dropped {k}')
st = os.stat(path)
open(path, 'wb').write(plistlib.dumps(cache, fmt=fmt))
os.chown(path, st.st_uid, st.st_gid); os.chmod(path, st.st_mode & 0o7777)
PY
sudo -n hdiutil detach "$dev"; dev=

st "layout (hashing the three images)"
venv/bin/python UniXNU-zizhan/scripts/prepare-ipados18-ufs-layout.py --userland "$P" --systemos "$P/pong-ipados18-systemos.cdr" \
    --output-dir "$U" --volume-layout "$P/volume-layout.json" > "$W/logs/layout-$TAG.log" 2>&1 || { st LAYOUT_FAIL; tail -20 "$W/logs/layout-$TAG.log"; exit 1; }

st "install --only OS (adb, phone in Android)"
python3 install_pinned.py --layout "$U/ufs-layout.json" --only OS > "$W/logs/install-$TAG.log" 2>&1 || { st INSTALL_FAIL; tail -20 "$W/logs/install-$TAG.log"; exit 1; }
tail -3 "$W/logs/install-$TAG.log" | sed 's/^/   /'
st JOBS_REFRESH_DONE
