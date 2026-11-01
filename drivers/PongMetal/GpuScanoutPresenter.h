// SPDX-License-Identifier: MIT
// GPU presentation: the GPU copies a finished BGRA frame (the compositor's IOSurface pages) into the firmware scanout memory. Both are
// imported without a copy (newBufferWithBytesNoCopy: pinned by the kernel and mapped into the GPU's 4 KiB context), the copy is a blit on
// the GPU's queue, and the call returns after the GPU retired it. No CPU touches a pixel; there is no staging copy and no software fallback
// here. Used through the public Metal API, on any device (the native frontend on the tablet, the Mac's device for the host check).
#pragma once
#import <Metal/Metal.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <mach/mach_time.h>

namespace gpupresent {

struct Scanout {
    void *base;       // writable mapping of the firmware framebuffer (page aligned)
    size_t bytes;     // whole extent, a multiple of the page size
    size_t stride;    // bytes per row
    uint32_t width, height;
};

struct Frame {
    const void *base; // page-aligned mapping of the surface pages
    size_t bytes;     // allocation size, a multiple of the page size
    size_t stride;
    uint32_t width, height;
    uint32_t token = 0;  // nonzero: the same pages come again under this token, keep the import
    uint32_t evict = 0;  // a kept import to drop before this frame
    size_t target = 0;   // byte offset in the scanout where the copy goes (a panel buffer under pong_dpu_flip; 0 otherwise)
};

// Time spent per stage since the last reset, in mach absolute time units.
struct Timing { uint64_t import = 0, encode = 0, wait = 0, frames = 0, reused = 0; };

enum Result { kOk = 0, kBadFrame, kBadScanout, kNoResource, kGpuFailed };

class Presenter {
public:
    Presenter(id<MTLDevice> device, const Scanout &scanout) : device_(device), scanout_(scanout) {
        queue_ = [device newCommandQueue];
        if (!validScanout() || !queue_) return;
        target_ = [device newBufferWithBytesNoCopy:scanout.base length:scanout.bytes options:MTLResourceStorageModeShared deallocator:nil];
    }
    bool ready() const { return target_ != nil; }
    // One frame: encodes the GPU copy, commits, waits for the GPU. Rows are copied as whole rows of width*4 bytes.
    Result present(const Frame &frame) {
        if (!ready()) return kBadScanout;
        const size_t page = (size_t)getpagesize(), row = (size_t)frame.width * 4;
        if (!frame.base || ((uintptr_t)frame.base & (page - 1)) || !frame.bytes || (frame.bytes & (page - 1)) ||
            frame.width != scanout_.width || frame.height != scanout_.height || frame.stride < row ||
            (size_t)frame.height > frame.bytes / frame.stride || frame.stride * (frame.height - 1) + row > frame.bytes) return kBadFrame;
        if ((frame.target & (page - 1)) || frame.target > scanout_.bytes || scanout_.stride * frame.height > scanout_.bytes - frame.target) return kBadFrame;
        if (frame.evict)
            for (auto &k : kept_)
                if (k.token == frame.evict) { k.token = 0; k.buffer = nil; }
        const uint64_t t0 = mach_absolute_time();
        id<MTLBuffer> source = nil;
        if (frame.token)
            for (auto &k : kept_)
                if (k.token == frame.token && k.buffer.length == frame.bytes) { source = k.buffer; break; }
        if (source) {
            timing.reused++;
        } else {
            source = [device_ newBufferWithBytesNoCopy:(void *)frame.base length:frame.bytes options:MTLResourceStorageModeShared deallocator:nil];
            if (!source) return kNoResource;
            if (frame.token) {
                // The kernel keeps at most as many mappings as there are slots here and names the one it drops.
                Kept *slot = &kept_[frame.token % kKept];
                for (auto &k : kept_) if (!k.token) { slot = &k; break; }
                slot->token = frame.token; slot->buffer = source;
            }
        }
        const uint64_t t1 = mach_absolute_time();
        id<MTLCommandBuffer> commands = [queue_ commandBuffer];
        id<MTLBlitCommandEncoder> blit = commands ? [commands blitCommandEncoder] : nil;
        if (!blit) return kNoResource;
        if (frame.stride == scanout_.stride) {
            // Equal pitch: the rows are one contiguous run.
            [blit copyFromBuffer:source sourceOffset:0 toBuffer:target_ destinationOffset:frame.target size:frame.stride * (frame.height - 1) + row];
        } else {
            // A linear texture over the surface pages reads them with the surface's pitch and writes the scanout's pitch.
            MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:frame.width
                                                                                       height:frame.height mipmapped:NO];
            d.storageMode = MTLStorageModeShared;
            d.usage = MTLTextureUsageShaderRead;
            if (frame.stride % [device_ minimumLinearTextureAlignmentForPixelFormat:MTLPixelFormatBGRA8Unorm]) {
                for (uint32_t y = 0; y < frame.height; ++y)
                    [blit copyFromBuffer:source sourceOffset:y * frame.stride toBuffer:target_ destinationOffset:frame.target + y * scanout_.stride size:row];
            } else {
                id<MTLTexture> texture = [source newTextureWithDescriptor:d offset:0 bytesPerRow:frame.stride];
                if (!texture) {[blit endEncoding]; return kNoResource;}
                [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                           sourceSize:MTLSizeMake(frame.width, frame.height, 1) toBuffer:target_ destinationOffset:frame.target
                    destinationBytesPerRow:scanout_.stride destinationBytesPerImage:scanout_.stride * frame.height];
            }
        }
        [blit endEncoding];
        [commands commit];
        const uint64_t t2 = mach_absolute_time();
        [commands waitUntilCompleted];
        const uint64_t t3 = mach_absolute_time();
        timing.import += t1 - t0; timing.encode += t2 - t1; timing.wait += t3 - t2; timing.frames++;
        if (commands.status != MTLCommandBufferStatusCompleted) {
            for (auto &k : kept_) { k.token = 0; k.buffer = nil; }
            return kGpuFailed;
        }
        return kOk;
    }
    Timing timing;
private:
    static constexpr unsigned kKept = 4;
    struct Kept { uint32_t token = 0; id<MTLBuffer> buffer = nil; };
    Kept kept_[kKept];
    bool validScanout() const {
        const size_t page = (size_t)getpagesize();
        return scanout_.base && !((uintptr_t)scanout_.base & (page - 1)) && scanout_.bytes && !(scanout_.bytes & (page - 1)) &&
               scanout_.width && scanout_.height && scanout_.stride >= (size_t)scanout_.width * 4 &&
               scanout_.stride * scanout_.height <= scanout_.bytes;
    }
    id<MTLDevice> device_;
    Scanout scanout_;
    id<MTLCommandQueue> queue_;
    id<MTLBuffer> target_;
};

}  // namespace gpupresent
