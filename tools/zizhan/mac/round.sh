#!/bin/bash
# Mac: build kernel + package + one unattended cycle. usage: round.sh RUN [extra kernel args...]
# ZIZHAN_ROOT_4K: the root image's first 4K block (install_pinned.py prints it; the default is the author's phone).
# ADSP=2 boots the ADSP and the battery (ci_boot_ufs.sh).
# Times every stage so slow steps show up in logs/RUN.run.out.
set -u
RUN=$1; shift
# Never boot a round without the autoreset: without pong_autoreset nothing (pinger, guard, deadline klog flush) is armed and the
# phone sits in iOS forever, which looked like four hard hangs on 10-31 (g152..g152d). Callers may still override both values.
case " $* " in *" pong_autoreset="*) ;; *) set -- pong_autoreset=300 pong_autoreset_sb=60 "$@";; esac
cd ${ZIZHAN_SRC:-$HOME/zizhan} || exit 1
L=${ZIZHAN_SRC:-$HOME/zizhan}/logs; mkdir -p $L
t0=$(date +%s); st() { echo "$(date +%T) +$(( $(date +%s) - t0 ))s $*"; }
st kernel build
DEFCONFIG=zizhan_ipados18_gpu_defconfig bash ci_kernel.sh > ci_kernel-$RUN.log 2>&1 || { st KERNEL_FAIL; grep -m20 -E "error|Error" ci_kernel-$RUN.log; tail -20 ci_kernel-$RUN.log; exit 1; }
st package
PROFILE=zizhan_ipados18_gpu bash ci_boot_ufs.sh boot-$RUN ${ZIZHAN_ROOT_4K:-14452736} pong_touch=1 "$@" > $L/$RUN.build.out 2>&1 || { st PACKAGE_FAIL; tail -20 $L/$RUN.build.out; exit 1; }
# Wait for the autoreset deadline plus boot and slack (11-01 v10: pong_autoreset_sb=600 outlived the fixed 420 s and the log needed
# cycle-resume.sh).
DEADLINE=$(printf '%s\n' "$@" | sed -n 's/^pong_autoreset=\([0-9]*\)$/\1/p' | head -1)
WAIT=$(( ${DEADLINE:-300} + 120 )); [ $WAIT -lt 420 ] && WAIT=420
st cycle
bash cycle.sh out/boot-$RUN/pong-xnu-initial.img $RUN $WAIT > $L/$RUN.cycle.out 2>&1; rc=$?
cat $L/$RUN.cycle.out
st "cycle rc=$rc"
