cd /dev/block/by-name || exit 1
for p in *; do t=$(readlink -f $p); s=$(blockdev --getsize64 $t); echo "$p $t $s"; done
for l in a b c d e f; do echo "LUN sd$l $(blockdev --getsize64 /dev/block/sd$l) $(cat /sys/block/sd$l/queue/logical_block_size)"; done
