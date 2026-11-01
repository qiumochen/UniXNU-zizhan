#!/bin/bash
# Mac: replicate UniXNU-pong CI (Path A: no Wi-Fi/backlight/Adreno) for zizhan, up to the kernel build.
set -euo pipefail
export PATH=/opt/homebrew/bin:$PATH HOMEBREW_NO_AUTO_UPDATE=1
SRC=${ZIZHAN_SRC:-$HOME/zizhan}
D=$SRC/UniXNU-zizhan
cd $SRC
step(){ echo "=== $(date +%T) $*"; }

step tools
brew list lzfse >/dev/null 2>&1 || brew install lzfse
[ -x $SRC/venv/bin/python3 ] || python3 -m venv $SRC/venv
$SRC/venv/bin/pip -q install --disable-pip-version-check pyhpke
export PATH=$SRC/venv/bin:$PATH

step sheng
if [ ! -d $SRC/sheng/package ]; then
  rm -rf sheng; mkdir sheng; git -C sheng init -q; git -C sheng remote add origin https://github.com/UniXNU/UniXNU-sheng.git
  git -C sheng fetch -q --depth 1 origin 08cde20edc3254ed6f7df8d0f5384fb887786e8f; git -C sheng checkout -q FETCH_HEAD
  git -C sheng submodule update --init --depth 1
fi

step refer pins
cd $D
python3 - <<'PY' | while IFS=$'\t' read -r ref url commit; do
import tomllib
from pathlib import Path
for s in tomllib.loads(Path('feeds.toml').read_text())['source']:
    if s.get('kind') == 'refer': print(f"{s['ref']}\t{s['git']}\t{s['commit']}")
PY
  dest="refer/ipados18/${ref}"
  if [ -d "$dest/.git" ] && [ "$(git -C "$dest" rev-parse HEAD)" = "$commit" ]; then echo "cached $ref"; continue; fi
  rm -rf "$dest"; mkdir -p "$dest"; git -C "$dest" init -q; git -C "$dest" remote add origin "$url"
  git -C "$dest" fetch -q --depth 1 origin "$commit"; git -C "$dest" checkout -q FETCH_HEAD
  test "$(git -C "$dest" rev-parse HEAD)" = "$commit"; echo "fetched $ref"
done

step DarwinBoot24
E=$SRC/uefi-zizhan/Mu_Basecore
test -x $E/BaseTools/Source/C/bin/GenFw
export PYTHON_COMMAND=$(command -v python3) IASL_PREFIX="$(brew --prefix acpica)/bin/" NASM_PREFIX="$(brew --prefix nasm)/bin/"
if [ ! -s mac-build/darwinboot24/DarwinBoot24.efi ]; then
  python3 scripts/build-darwinboot24-macos.py --edk2 $E --package $SRC/UniXNU-pin/package --llvm /opt/homebrew/opt/llvm@18/bin --output-dir $SRC/work/Pong-DarwinBoot24
fi
ls -l mac-build/darwinboot24/DarwinBoot24.efi

step layout and kernelcache
export PONG_WORK=$SRC/work/pong-work PONG_INPUTS=$SRC/work/pong-inputs PONG_DEVELOPER_DIR="$(xcode-select -p)"
export PONG_BUILD_TREE=$PONG_WORK/build/Pong-iPadOS18-native/tree PONG_VOLUME_LAYOUT=$PONG_WORK/build/volume-layout.json
export PONG_STORAGE_HEADERS=$D/refer/ipados18/IOStorageFamily-323 PONG_SHENG_ROOT=$SRC/sheng
export PONG_WIFI_BUILD=$PONG_WORK/wifi-ath11k-build PONG_LLVM=/opt/homebrew/opt/llvm@18/bin PONG_LLVM_NM=/opt/homebrew/opt/llvm@18/bin/llvm-nm PONG_LLVM_OBJCOPY=/opt/homebrew/opt/llvm@18/bin/llvm-objcopy
mkdir -p $PONG_WORK/build $PONG_INPUTS
python3 - <<'PY'
import json, uuid, os
from pathlib import Path
p = Path(os.environ['PONG_VOLUME_LAYOUT'])
if not p.exists():
    ns = uuid.UUID('6f1b6a3e-7c2d-4a91-9e5b-0c4d8f2a1b77')
    p.write_text(json.dumps({'ci_placeholder': True, 'note': 'uuid5 placeholders (zizhan first boot, no root volume yet)',
        'os': {'uuid': str(uuid.uuid5(ns, 'os'))}, 'data': {'uuid': str(uuid.uuid5(ns, 'data'))},
        'systemos': {'uuid': str(uuid.uuid5(ns, 'systemos'))}}, indent=2) + '\n')
Path('local.toml').write_text('[pong_build]\ntree = %s\ndeveloper_dir = %s\nvolume_layout = %s\n' % tuple(
    json.dumps(os.environ[k]) for k in ('PONG_BUILD_TREE', 'PONG_DEVELOPER_DIR', 'PONG_VOLUME_LAYOUT')))
PY
KC=apple/ipados18-22H374/components/kernelcache.release.ipad7c.decompressed/kernelcache.release.ipad7c.decompressed
if [ ! -s $KC ]; then
  python3 scripts/prepare-ipados18-components.py --remote --output-dir $PONG_INPUTS/22H374 --only KernelCache
  python3 scripts/decompress-ipados18-kernelcache.py --im4p $PONG_INPUTS/22H374/components/kernelcache.release.ipad7c --output $KC
fi
ls -l $KC
env | grep '^PONG_' | sed 's/^/export /' > $SRC/ci-env.sh
echo PREP_DONE
