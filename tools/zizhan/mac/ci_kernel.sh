#!/bin/bash
# Mac: compile the Darwin24 kernel for the zizhan device dir (Pong CI Path A defconfig).
set -euo pipefail
SRC=${ZIZHAN_SRC:-$HOME/zizhan}
export PATH=$SRC/venv/bin:/opt/homebrew/bin:$PATH
source $SRC/ci-env.sh
cd $SRC/UniXNU-zizhan
python3 scripts/build-pong-format2.py --unixnu $SRC/UniXNU-pin --out $SRC/UniXNU-zizhan/out --jobs ${JOBS:-6} --defconfig ${DEFCONFIG:-pong_ipados18_ci_defconfig}
echo KERNEL_DONE
