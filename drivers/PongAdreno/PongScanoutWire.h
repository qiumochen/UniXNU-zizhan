/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Fixed records adapted from NativeMobileDisplay/NativeGOPWire.h. */
#pragma once
#include <stdint.h>
enum {PongScanoutVersion=1,PongScanoutBgra=1,PongScanoutMode=0x7000,
      PongScanoutWait=0x7002,PongScanoutComplete=0x7003,PongScanoutMemory=0x5343};
// A presenter rejected the frame before committing any GPU command. This is
// distinct from a failed/unknown fence: CPU fallback cannot race DMA here.
enum : uint32_t {PongScanoutNotSubmitted=0x4e4f444d};
struct PongScanoutModeInfo {uint32_t version,format;uint64_t width,height,stride,bytes;};
struct PongScanoutWaitInfo {uint64_t timeout_ms;};
// token: nonzero when the mapping is kept for later frames of the same surface
// (the presenter may keep its GPU import under that token); evict: a token the
// presenter must drop before this frame, its mapping goes once the frame retires.
// target: byte offset in the scanout mapping (PongScanoutMemory) the copy goes to;
// 0 without panel double buffering (pong_dpu_flip), else one of the panel buffers
// the kernel flips the display to once the copy has retired (was reserved, always 0).
struct PongScanoutFrame {uint64_t sequence,address,bytes,stride;uint32_t width,height,format,token,evict,target;};
struct PongScanoutDone {uint64_t sequence;uint32_t status,reserved;};
static_assert(sizeof(PongScanoutModeInfo)==40 && sizeof(PongScanoutWaitInfo)==8 &&
    sizeof(PongScanoutFrame)==56 && sizeof(PongScanoutDone)==16,"Pong scanout wire");
