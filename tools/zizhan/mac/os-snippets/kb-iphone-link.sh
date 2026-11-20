# os-edit.sh snippet (runs as root, $M = mounted system volume): TextInputUI picks KBLayouts_<MobileGestalt DeviceClass>.dat;
# this board answers DeviceClass iPhone (iPhone17,1 identity) but the iPadOS root ships only KBLayouts_iPad.dat, so apps got an
# empty keyboard and kbd crashed in Favonius perform_search. Point the iPhone name at the iPad layouts.
T="$M/System/Library/PrivateFrameworks/TextInputUI.framework"
[ -f "$T/KBLayouts_iPad.dat" ] || { echo "no KBLayouts_iPad.dat"; exit 1; }
if [ -e "$T/KBLayouts_iPhone.dat" ] || [ -L "$T/KBLayouts_iPhone.dat" ]; then echo "KBLayouts_iPhone.dat already present"; else ln -s KBLayouts_iPad.dat "$T/KBLayouts_iPhone.dat" || exit 1; fi
ls -la "$T"/KBLayouts*
