#!/bin/bash
# Mac: restore launchd cache entries and job plists from a kept prepared OS image.
# Usage: job-restore.sh TAG FROMTAG LABEL...
# Requires a full phone backup and Android/root, as install_pinned.py does.
# Saves the current image/layout under TAG, then installs only the pinned root's OS window.
# Does not change build-ipados18.py's EXCLUDED18 policy for later full rebuilds.
set -euo pipefail
if [ "$#" -lt 3 ]; then echo "usage: $0 TAG FROMTAG LABEL..." >&2; exit 2; fi
TAG=$1; FROM=$2; shift 2
for tag in "$TAG" "$FROM"; do
  case "$tag" in ''|*[!a-zA-Z0-9_-]*) echo 'tags must contain only letters, digits, _ or -' >&2; exit 2;; esac
done
SRC=${ZIZHAN_SRC:-$HOME/zizhan}
cd "$SRC"
export PATH=/opt/homebrew/bin:$PATH
P=work/userland-22H374/prepared; U=work/userland-22H374/ufs-metal; W=work/gpu
old="$P/pong-ipados18-system.$FROM.cdr"
current="$P/pong-ipados18-system.cdr"
backup="$P/pong-ipados18-system.$TAG.cdr"
[ -f "$old" ] && [ -f "$current" ] && [ -f "$U/ufs-layout.json" ]
[ ! -e "$backup" ] && [ ! -e "$U/ufs-layout.$TAG.json" ] || { echo 'backup tag already exists' >&2; exit 1; }
bash "$SRC/UniXNU-zizhan/tools/zizhan/mac/adb-visible.sh" || { echo 'Android adb device required before editing the OS image' >&2; exit 1; }
mkdir -p "$W/logs"
cp -c "$current" "$backup"
cp "$U/ufs-layout.json" "$U/ufs-layout.$TAG.json"
mounts=$(mktemp -d "$P/job-restore.XXXXXX")
mkdir "$mounts/from" "$mounts/current"
srcdev=; dstdev=
cleanup() {
  [ -z "$dstdev" ] || sudo -n hdiutil detach "$dstdev" || true
  [ -z "$srcdev" ] || sudo -n hdiutil detach "$srcdev" || true
  rmdir "$mounts/from" "$mounts/current" "$mounts" 2>/dev/null || true
}
trap cleanup EXIT
# Parse the device only after a successful attach (pipefail propagates mount failures).
srcdev=$(sudo -n hdiutil attach -readonly -nobrowse -noverify -mountpoint "$mounts/from" "$old" | awk 'NR==1{print $1}')
dstdev=$(sudo -n hdiutil attach -readwrite -nobrowse -noverify -owners on -mountpoint "$mounts/current" "$current" | awk 'NR==1{print $1}')
[ -n "$srcdev" ] && [ -n "$dstdev" ]
sudo -n python3 - "$mounts/from" "$mounts/current" "$@" <<'PY'
import os
import plistlib
import shutil
import sys
from pathlib import Path

old, new = map(Path, sys.argv[1:3])
labels = set(sys.argv[3:])
relative = Path('System/Library/xpc/launchd.plist')
path = new / relative
raw = path.read_bytes()
fmt = plistlib.FMT_BINARY if raw.startswith(b'bplist') else plistlib.FMT_XML
cache = plistlib.loads(raw)
source = plistlib.loads((old / relative).read_bytes())
jobs = {k: j for k, j in source['LaunchDaemons'].items() if j.get('Label') in labels}
missing = labels - {j['Label'] for j in jobs.values()}
if missing:
    raise SystemExit('labels absent from source: ' + ', '.join(sorted(missing)))
# Check every source plist before modifying the destination.
for key in jobs:
    relative_job = Path(key.lstrip('/'))
    if '..' in relative_job.parts or relative_job.parent != Path('System/Library/LaunchDaemons'):
        raise SystemExit('unexpected job path: ' + key)
    if not (old / relative_job).is_file():
        raise SystemExit('source plist missing: ' + key)
for key, job in jobs.items():
    destination = new / key.lstrip('/')
    shutil.copy2(old / key.lstrip('/'), destination)
    os.chown(destination, 0, 0)
    cache['LaunchDaemons'][key] = job
    print('restored', key)
st = path.stat()
path.write_bytes(plistlib.dumps(cache, fmt=fmt))
os.chown(path, st.st_uid, st.st_gid)
os.chmod(path, st.st_mode & 0o7777)
PY
sudo -n hdiutil detach "$dstdev"; dstdev=
sudo -n hdiutil detach "$srcdev"; srcdev=
venv/bin/python UniXNU-zizhan/scripts/prepare-ipados18-ufs-layout.py \
  --userland "$P" --systemos "$P/pong-ipados18-systemos.cdr" \
  --output-dir "$U" --volume-layout "$P/volume-layout.json" > "$W/logs/layout-$TAG.log" 2>&1
python3 UniXNU-zizhan/tools/zizhan/mac/install_pinned.py --layout "$U/ufs-layout.json" --only OS \
  > "$W/logs/install-$TAG.log" 2>&1
tail -3 "$W/logs/install-$TAG.log"
echo RESTORE_DONE
