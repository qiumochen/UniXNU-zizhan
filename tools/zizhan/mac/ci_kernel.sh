#!/bin/bash
# Mac: compile the default zizhan GPU + Wi-Fi kernel; CI selects its reduced defconfig explicitly.
set -euo pipefail
SRC=${ZIZHAN_SRC:-$HOME/zizhan}
export PATH=$SRC/venv/bin:/opt/homebrew/bin:$PATH
source $SRC/ci-env.sh
cd $SRC/UniXNU-zizhan
python3 scripts/build-pong-format2.py --unixnu $SRC/UniXNU-pin --out $SRC/UniXNU-zizhan/out --jobs ${JOBS:-6} --defconfig ${DEFCONFIG:-zizhan_ipados18_gpu_defconfig}
echo KERNEL_DONE
