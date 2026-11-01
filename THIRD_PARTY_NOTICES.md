# Third-party source notices

This device repository contains or builds against these separately licensed projects:

- UniXNU core, DarwinBoot, NativePrelink, NativeUfs, NativeRamBlock, and the Adreno modules: retain their per-file SPDX identifiers and upstream repository licenses.
- NativeMetal, NativeAdreno, and ArmSmmu4K implementations: retain the upstream UniXNU module licenses.
- iPadOS 18.7.10 / 22H374 components are obtained from Apple's restore service by the scripts; no Apple restore binaries are included here.
- This repository is derived from UniXNU-pong (https://github.com/Chengdong-Yu/UniXNU-pong) and keeps its history; files from it keep their authors and licenses.
- `patches/xnu-ipados18/` modifies Apple's XNU, which is under the Apple Public Source License 2.0; the patches and the files they add (such as `bsd/dev/pong_fts.c`) are distributed under the same license as the XNU files they change.

The device-level license does not replace a component's file-level license.
