#!/bin/bash
# Mac: wait (up to 12 h) until the phone shows up on USB again, then finish the pending round RUN.
# WHEN_BACK_IMAGE selects an already packed image for both Android and fastboot recovery.
# Without it, use the historical round.sh path from Android or boot-RUN image from fastboot.
# usage: [WHEN_BACK_IMAGE=path] when-back.sh RUN ARGS...
export PATH=/opt/homebrew/bin:$PATH
S=${ZIZHAN_SRC:-$HOME/zizhan}
cd "$S" || exit 1
RUN=$1; shift
IMAGE=${WHEN_BACK_IMAGE:-}
[ -z "$IMAGE" ] || [ -f "$IMAGE" ] || { echo "missing image: $IMAGE"; exit 2; }
deadline=$((SECONDS + 43200))
while [ "$SECONDS" -lt "$deadline" ]; do
  if bash "$S/fastboot-visible.sh"; then
    echo "$(date +%T) back in fastboot"
    exec bash cycle.sh "${IMAGE:-out/boot-$RUN/pong-xnu-initial.img}" "$RUN" 420
  fi
  if adb devices | grep -q "device$"; then
    echo "$(date +%T) back in Android"
    sleep 20
    if [ -n "$IMAGE" ]; then exec bash cycle.sh "$IMAGE" "$RUN" 420; fi
    exec env ADSP=2 bash round.sh "$RUN" "$@"
  fi
  sleep 10
done
