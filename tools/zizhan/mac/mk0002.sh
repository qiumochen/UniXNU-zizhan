#!/bin/bash
# Mac: copy pong_fts.c into the XNU work tree and regenerate patches/xnu-ipados18/0002-zizhan-fts.patch
# (only the FTS file plus its two hook hunks, so 0001's idempotence check keeps passing).
set -euo pipefail
SRC=${ZIZHAN_SRC:-$HOME/zizhan}
W=$SRC/work/pong-work/build/Pong-iPadOS18-native/tree/.cache/xnu-work
cp "${1:-/tmp/pong_fts.c}" $W/bsd/dev/pong_fts.c
P=/tmp/p2; rm -rf $P/b/bsd/dev; mkdir -p $P/b/bsd/dev; cp $W/bsd/dev/pong_fts.c $P/b/bsd/dev/
cd $P
{ diff -u --label a/bsd/conf/files --label b/bsd/conf/files a/bsd/conf/files b/bsd/conf/files
  diff -u --label a/bsd/kern/bsd_init.c --label b/bsd/kern/bsd_init.c a/bsd/kern/bsd_init.c b/bsd/kern/bsd_init.c
  diff -u --label /dev/null --label b/bsd/dev/pong_fts.c /dev/null b/bsd/dev/pong_fts.c; } > /tmp/0002.patch || true
git -C $W apply --reverse --check /tmp/0002.patch
cp /tmp/0002.patch $SRC/UniXNU-zizhan/patches/xnu-ipados18/0002-zizhan-fts.patch
echo "0002 regenerated ($(wc -l < /tmp/0002.patch) lines)"
