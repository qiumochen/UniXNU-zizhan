/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_DSP_FIRMWARE_POLICY_H
#define ZIZHAN_DSP_FIRMWARE_POLICY_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Exact vnode paths only. This does not install firmware or enable a DSP.
static inline int zizhan_dsp_firmware_path(const char *path) {
    return path && (!strcmp(path, "/pong/firmware/adsp.mbn") ||
                    !strcmp(path, "/pong/firmware/slpi.mbn"));
}
static inline int zizhan_dsp_firmware_read_range(uint64_t offset, size_t bytes) {
    const uint64_t limit = 64ull * 1024 * 1024;
    return bytes <= 1024 * 1024 && offset <= limit && bytes <= limit - offset;
}
#endif
