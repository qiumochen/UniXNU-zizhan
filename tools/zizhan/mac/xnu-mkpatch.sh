#!/bin/bash
# SPDX-License-Identifier: BSD-2-Clause-Patent
# Mac: turn edits made in the generated XNU work tree into patches/xnu-ipados18/NNNN-name.patch, BEFORE the next build.
#
# usage: xnu-mkpatch.sh snapshot NNNN FILE...        copy the unedited FILEs aside (run this first, then edit them in xnu-work)
#        xnu-mkpatch.sh NNNN-name.patch "summary line" FILE...     write the patch from the snapshot, add it to series, check it
#
# FILE is relative to the XNU tree ($ZIZHAN_SRC/work/pong-work/build/Pong-iPadOS18-native/tree/.cache/xnu-work). The kernel
# build reverses every patch from 0002 up and applies them again, so an edit that is not a patch file yet leaves the tree half
# reversed ("Patch does not apply cleanly ... 0001", 2026-11-23). Snapshots live in $ZIZHAN_SRC/work/xnu-snap/<file>.pre<NNNN>.
set -euo pipefail
cd "${ZIZHAN_SRC:?}"
T=$PWD/work/pong-work/build/Pong-iPadOS18-native/tree; X=$T/.cache/xnu-work; P=$PWD/UniXNU-zizhan/patches/xnu-ipados18
SNAP=$PWD/work/xnu-snap; mkdir -p "$SNAP"
if [ "${1:-}" = snapshot ]; then
  n=${2:?number}; shift 2
  for F in "$@"; do
    s=$SNAP/$(echo "$F" | tr / _).pre$n
    [ -e "$s" ] && { echo "snapshot exists: $s"; exit 1; }
    cp "$X/$F" "$s"; echo "snapshot $s"
  done
  exit 0
fi
name=${1:?NNNN-name.patch}; msg=${2:?summary}; shift 2; n=${name%%-*}
tmp=$(mktemp -d)
for F in "$@"; do
  s=$SNAP/$(echo "$F" | tr / _).pre$n
  [ -e "$s" ] || { echo "no snapshot for $F ($s): run '$0 snapshot $n $F' before editing"; exit 1; }
  mkdir -p "$tmp/a/$(dirname "$F")" "$tmp/b/$(dirname "$F")"; cp "$s" "$tmp/a/$F"; cp "$X/$F" "$tmp/b/$F"
done
( echo "$msg"; cd "$tmp" && for F in "$@"; do diff -u "a/$F" "b/$F" || true; done | python3 -c "
import sys
for l in sys.stdin:
    if l.startswith('--- a/') or l.startswith('+++ b/'): l = l.split('\t')[0] + '\n'
    sys.stdout.write(l)" ) > "$P/$name"
rm -rf "$tmp"
grep -q "^$name$" "$P/series" || echo "$name" >> "$P/series"
git -C "$X" apply --reverse --check "$P/$name" && echo "ok: $name applies in reverse, $(grep -c '^@@' "$P/$name") hunks"
python3 UniXNU-zizhan/tools/zizhan/tests/xnu-patch-format.py 2>&1 | tail -1
