#!/bin/bash
# Decode only screenshots contained in the freshly retrieved RUN.txt.
# Remove same-run PNGs first so a later round with no captures cannot inherit them.
set -euo pipefail
if [ "$#" -ne 1 ]; then echo "usage: $0 RUN" >&2; exit 2; fi
RUN=$1
case "$RUN" in ''|*[!a-zA-Z0-9_-]*) echo 'run must contain only letters, digits, _ or -' >&2; exit 2;; esac
S=${ZIZHAN_SRC:-$HOME/zizhan}
L="$S/logs"
LOG="$L/$RUN.txt"
[ -s "$LOG" ] || { echo "missing round log: $LOG" >&2; exit 1; }
shopt -s nullglob
old=("$L/$RUN".shot*.png)
if [ "${#old[@]}" -gt 0 ]; then rm -f -- "${old[@]}"; fi
python3 "$(dirname "${BASH_SOURCE[0]}")/shot.py" "$LOG" "$L"
shots=("$L/$RUN".shot*.png)
echo "shot decode: ${#shots[@]} PNG(s) from $RUN.txt"
