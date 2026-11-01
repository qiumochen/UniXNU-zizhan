#!/bin/bash
# Mac: build the zizhan UEFI FD directly with EDK II (mirrors UniXNU scripts/unixnu/firmware.py).
set -euo pipefail
export PATH=/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:$PATH
SRC=${ZIZHAN_SRC:-$HOME/zizhan}
U=$SRC/uefi-zizhan; E=$U/Mu_Basecore; T=$E/BaseTools
PIN=0fc0fc1f9ddd0fbd4787738afef38667df5e0a95   # UniXNU commit pinned by UniXNU-pong README
if [ ! -d $SRC/UniXNU-pin/package/UniXNUPkg ]; then
  rm -rf $SRC/UniXNU-pin; mkdir -p $SRC/UniXNU-pin; cd $SRC/UniXNU-pin
  git init -q; git remote add origin https://github.com/UniXNU/UniXNU.git
  git fetch -q --depth 1 origin $PIN; git checkout -q FETCH_HEAD
fi
PK=$SRC/UniXNU-pin/package
DB=$SRC/UniXNU-zizhan/drivers/DarwinBoot24
[ -f $T/Source/C/bin/GenFw ] || make -C $T/Source/C -j4 > $SRC/basetools.log 2>&1
CONF=$SRC/edk2-conf; mkdir -p $CONF
for n in target tools_def build_rule; do [ -f $CONF/$n.txt ] || cp $T/Conf/$n.template $CONF/$n.txt; done
export WORKSPACE=$U EDK_TOOLS_PATH=$T CONF_PATH=$CONF PYTHON_COMMAND=python3
export PACKAGES_PATH=$U:$PK:$DB:$E:$U/Platforms/Xiaomi:$U/Platforms/Nothing:$U/Common/Mu:$U/Common/Mu_OEM_Sample:$U/Silicon/Qualcomm:$U/Silicon/Silicium:$U/Silicium-ACPI:$U/Silicium-ACPI/Platforms/Nothing:$U/Silicium-ACPI/Silicon/Qualcomm
export PATH=$T/BinWrappers/PosixLike:$T/Source/C/bin:$PATH
TC=$SRC/toolchain/bin; if [ ! -e $TC/lld-link ]; then mkdir -p $TC; ln -sf /opt/homebrew/opt/llvm/bin/* $TC/; ln -sf /opt/homebrew/opt/lld/bin/* $TC/; fi
export CLANG_BIN=$TC/
export SOURCE_DATE_EPOCH=$(git -C $U log -1 --format=%ct)
applied=()
cleanup(){ for p in "${applied[@]}"; do git -C $E apply --reverse "$p" || true; done; }
trap cleanup EXIT
for p in AsmMacroLib Auth-Service Boot-Manager Timer Usb-Bus; do
  f=$U/Resources/MuPatches/$p.patch; git -C $E apply --check $f && git -C $E apply $f && applied+=("$f")
done
cd $U
build -p Platforms/Xiaomi/zizhanPkg/zizhan.dsc -a AARCH64 -b ${TARGET:-RELEASE} -t CLANGPDB -n ${JOBS:-4} \
  -D FD_BASE=0xA7100000 -D FD_SIZE=0x300000 -D FD_BLOCKS=768 -D DEVICE_MODEL=0 -D ENABLE_SECUREBOOT=0 \
  -D MEMORY_PROTECTION=TRUE -D SHIP_MODE=FALSE -D BUILDID_STRING=UniXNU-Zizhan -D PONG_INITRD_DISK_ENABLED=1
ls -la Build/zizhanPkg/${TARGET:-RELEASE}_CLANGPDB/FV/SILICIUM_UEFI.fd
