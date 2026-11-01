#!/bin/bash
# Mac: wait (up to 12 h) until the phone shows up on USB again, then finish the pending round RUN: from fastboot boot its built image
# (cycle.sh), from Android run the whole round again (round.sh RUN ARGS...). usage: when-back.sh RUN ARGS...
export PATH=/opt/homebrew/bin:$PATH
cd ${ZIZHAN_SRC:-$HOME/zizhan} || exit 1
RUN=$1; shift
for i in $(seq 1 4320); do
  if fastboot devices | grep -q fastboot; then echo "$(date +%T) back in fastboot"; exec bash cycle.sh out/boot-$RUN/pong-xnu-initial.img $RUN 420; fi
  if adb devices | grep -q "device$"; then echo "$(date +%T) back in Android"; sleep 20; exec env ADSP=2 bash round.sh $RUN "$@"; fi
  sleep 10
done
