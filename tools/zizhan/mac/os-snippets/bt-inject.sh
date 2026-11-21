# os-trial-edit.py snippet (runs as root, $M = mounted system volume, BT_DYLIB = the built library): BT-01 puts the BlueTool
# MobileGestalt interposer at /pong/bt.dylib; XNU 0031 inserts it into BlueTool only when boot-arg pong_inj=/pong/bt.dylib:BlueTool.
[ -f "$BT_DYLIB" ] || { echo "BT_DYLIB missing"; exit 1; }
cp "$BT_DYLIB" "$M/pong/bt.dylib" && chown 0:0 "$M/pong/bt.dylib" && chmod 755 "$M/pong/bt.dylib" || exit 1
ls -la "$M/pong/bt.dylib"
