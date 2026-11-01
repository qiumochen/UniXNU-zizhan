# Sourced by the Mac scripts. ZIZHAN_SRC is the work directory that holds UniXNU-zizhan, uefi-zizhan,
# UniXNU-pin (UniXNU at the pinned commit), sheng (UniXNU-sheng), venv/ and work/ (default ~/zizhan).
SRC=${ZIZHAN_SRC:-$HOME/zizhan}
export PONG_WORK=$SRC/work/pong-work
export PONG_BUILD_TREE=$SRC/work/pong-work/build/Pong-iPadOS18-native/tree
export PONG_DEVELOPER_DIR=${PONG_DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}
export PONG_SHENG_ROOT=$SRC/sheng
export PONG_VOLUME_LAYOUT=$SRC/work/pong-work/build/volume-layout.json
export PONG_LLVM=/opt/homebrew/opt/llvm@18/bin
export PONG_STORAGE_HEADERS=$SRC/UniXNU-zizhan/refer/ipados18/IOStorageFamily-323
export PONG_LLVM_NM=/opt/homebrew/opt/llvm@18/bin/llvm-nm
export PONG_LLVM_OBJCOPY=/opt/homebrew/opt/llvm@18/bin/llvm-objcopy
export PONG_INPUTS=$SRC/work/pong-inputs
export PONG_WIFI_BUILD=$SRC/work/pong-work/wifi-ath11k-build
# Adreno 730 GMU/SQE/ZAP firmware from the phone's own vendor partitions (proprietary-files.txt).
export PONG_ADRENO_FIRMWARE=$SRC/work/firmware/adreno730
