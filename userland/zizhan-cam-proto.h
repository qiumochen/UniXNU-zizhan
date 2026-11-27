/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Track 3 (CAM-02): Shared kernel/userland camera streaming frame header, real-time closed-loop
 * Auto-Exposure (AE) & Gray-World Auto-White-Balance (AWB) + sRGB gamma RAW10->BGRA8888 soft-ISP,
 * and opt-in ISO QR test overlay (zero OS headers; host-tested under ASan/UBSan). */
#ifndef ZIZHAN_CAM_PROTO_H
#define ZIZHAN_CAM_PROTO_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ZIZHAN_CAM_MAGIC              = 0x5a43414du, /* 'ZCAM' */
    ZIZHAN_CAM_VERSION            = 1u,
    ZIZHAN_CAM_CMD_QUERY          = -1,
    ZIZHAN_CAM_CMD_OFF            = 0,
    ZIZHAN_CAM_CMD_BACK           = 1, /* 1x Main IMX766 50MP (with resilient OV13B10 fallback) */
    ZIZHAN_CAM_CMD_FRONT          = 2, /* Front IMX596 selfie */
    ZIZHAN_CAM_CMD_BACK_QR        = 3, /* 1x Main / Ultra live optical + corner ISO QR badge */
    ZIZHAN_CAM_CMD_ULTRA          = 4, /* 0.6x Rear Ultra-Wide OV13B10 */
    ZIZHAN_CAM_CMD_AF             = 5, /* Trigger closed-loop DW9800V VCM autofocus sweep */
    ZIZHAN_CAM_CMD_HAPTIC_BASE    = 0x100, /* 0x101..0x107 -> PM8350B LRA haptic patterns 1..7 */
    ZIZHAN_CAM_CMD_AUDIO_SPEAKER  = 0x200, /* Dual CS35L41 stereo speaker mode */
    ZIZHAN_CAM_CMD_AUDIO_RECEIVER = 0x201, /* Top CS35L41 0x40 earpiece receiver mode (-12 dB) */
    ZIZHAN_CAM_OUT_W              = 960u,
    ZIZHAN_CAM_OUT_H              = 720u,
    ZIZHAN_CAM_OUT_STRIDE         = 960u * 4u,
    ZIZHAN_CAM_OUT_BYTES          = 960u * 720u * 4u, /* 2,764,800 bytes BGRA8888 */
    ZIZHAN_CAM_TOTAL_BYTES        = 64u + (960u * 720u * 4u)
};

enum {
    ZIZHAN_CAM_FLAG_OPTICAL     = (1u << 0),
    ZIZHAN_CAM_FLAG_QR_STAMP    = (1u << 1),
    ZIZHAN_CAM_FLAG_FALLBACK    = (1u << 2),
    ZIZHAN_CAM_FLAG_MAIN_IMX766 = (1u << 3),
    ZIZHAN_CAM_FLAG_AF_LOCKED   = (1u << 4)
};

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t sensor_pos;     /* 1 = back 1x, 2 = front, 4 = back 0.6x ultra */
    uint16_t width;
    uint16_t height;
    uint32_t stride;         /* width * 4 (BGRA8888, little-endian B,G,R,A) */
    uint32_t seq;            /* monotonically increasing frame index */
    uint32_t exposure_lines; /* current sensor coarse integration lines */
    uint32_t analog_gain_q8; /* current sensor analog gain (256 = 1.0x) */
    uint16_t awb_r_q8;       /* Gray-World red gain (256 = 1.0x) */
    uint16_t awb_b_q8;       /* Gray-World blue gain (256 = 1.0x) */
    uint16_t mean_raw10;     /* 0..1023 pre-gamma Bayer mean */
    uint16_t mean_luma8;     /* 0..255 post-ISP BGRA luma mean */
    uint16_t flags;
    uint16_t af_dac;         /* current DW9800V 10-bit VCM DAC code (0 if fixed-focus) */
    uint64_t timestamp_ns;
    uint8_t  reserved[16];
} ZizhanCamFrameHeader;

typedef struct {
    uint32_t exposure_lines;
    uint32_t min_lines;
    uint32_t max_lines;
    uint32_t gain_q8;        /* 256 = 1.0x .. 4096 = 16.0x */
    uint32_t min_gain_q8;
    uint32_t max_gain_q8;
    uint16_t target_raw10;   /* target optical signal above black_level (default 150) */
    uint16_t black_level;    /* sensor black pedestal (64 on 10-bit = 0x40) */
} ZizhanCamAeState;

static inline void zizhan_cam_ae_init(ZizhanCamAeState *ae, uint32_t init_lines, uint32_t max_lines,
                                      uint32_t init_gain_q8, uint32_t max_gain_q8) {
    if (!ae) return;
    ae->exposure_lines = init_lines ? init_lines : 800u;
    ae->min_lines      = 16u;
    ae->max_lines      = max_lines >= 64u ? max_lines : 1580u;
    ae->gain_q8        = init_gain_q8 >= 256u ? init_gain_q8 : 512u;
    ae->min_gain_q8    = 256u;
    ae->max_gain_q8    = max_gain_q8 >= 256u ? max_gain_q8 : 3968u;
    ae->target_raw10   = 160u;
    ae->black_level    = 64u;
}

/* Damped proportional AE step: adjusts exposure lines first (flicker-friendly smooth ramp),
 * then analog gain when exposure reaches max_lines. Returns 1 if sensor regs should update. */
static inline int zizhan_cam_ae_step(ZizhanCamAeState *ae, uint16_t measured_mean10) {
    if (!ae) return 0;
    const int32_t sig = (measured_mean10 > ae->black_level)
                        ? (int32_t)(measured_mean10 - ae->black_level) : 1;
    const int32_t tgt = (ae->target_raw10 > 16u) ? (int32_t)ae->target_raw10 : 160;
    /* Deadband +-15% around target to avoid hunting */
    if (sig * 100 >= tgt * 85 && sig * 100 <= tgt * 115) return 0;

    /* Clamp per-step ratio to [0.5x .. 2.0x] (damped 50% toward target: ratio_q8 in [160..384]) */
    uint32_t ratio_q8 = (uint32_t)((tgt * 256) / (sig > 4 ? sig : 4));
    if (ratio_q8 < 128u) ratio_q8 = 128u;
    if (ratio_q8 > 512u) ratio_q8 = 512u;
    /* Damped blend: 50% current + 50% desired */
    ratio_q8 = (256u + ratio_q8) / 2u;

    const uint64_t cur_ev = (uint64_t)ae->exposure_lines * (uint64_t)ae->gain_q8;
    uint64_t new_ev = (cur_ev * ratio_q8) >> 8;
    const uint64_t min_ev = (uint64_t)ae->min_lines * (uint64_t)ae->min_gain_q8;
    const uint64_t max_ev = (uint64_t)ae->max_lines * (uint64_t)ae->max_gain_q8;
    if (new_ev < min_ev) new_ev = min_ev;
    if (new_ev > max_ev) new_ev = max_ev;

    uint32_t next_lines = (uint32_t)(new_ev / ae->min_gain_q8);
    uint32_t next_gain  = ae->min_gain_q8;
    if (next_lines > ae->max_lines) {
        next_lines = ae->max_lines;
        next_gain  = (uint32_t)(new_ev / next_lines);
        if (next_gain < ae->min_gain_q8) next_gain = ae->min_gain_q8;
        if (next_gain > ae->max_gain_q8) next_gain = ae->max_gain_q8;
    } else if (next_lines < ae->min_lines) {
        next_lines = ae->min_lines;
    }

    const int changed = (next_lines != ae->exposure_lines) || (next_gain != ae->gain_q8);
    ae->exposure_lines = next_lines;
    ae->gain_q8        = next_gain;
    return changed;
}

/* Compute clamped Gray-World AWB red & blue gains (Q8, 256 = 1.0x) from Bayer channel means. */
static inline void zizhan_cam_awb_gains(uint16_t mean_r, uint16_t mean_g, uint16_t mean_b,
                                        uint16_t black_level, uint16_t *r_q8_out, uint16_t *b_q8_out) {
    const int32_t r = (int32_t)mean_r - (int32_t)black_level;
    const int32_t g = (int32_t)mean_g - (int32_t)black_level;
    const int32_t b = (int32_t)mean_b - (int32_t)black_level;
    uint32_t rq = 256u, bq = 256u;
    if (g > 8 && r > 4 && b > 4) {
        rq = (uint32_t)((g * 256) / r);
        bq = (uint32_t)((g * 256) / b);
        if (rq < 140u) rq = 140u;
        if (rq > 460u) rq = 460u;
        if (bq < 140u) bq = 140u;
        if (bq > 460u) bq = 460u;
    }
    if (r_q8_out) *r_q8_out = (uint16_t)rq;
    if (b_q8_out) *b_q8_out = (uint16_t)bq;
}

/* Fast integer sqrt for gamma ~2.0 (maps 0..65535 -> 0..255 with natural shadow lift, zero FPU in kernel!). */
static inline uint8_t zizhan_cam_isqrt16(uint32_t v) {
    if (v >= 65025u) return 255u;
    uint32_t res = 0u, bit = 1u << 14;
    while (bit > v) bit >>= 2;
    while (bit != 0u) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint8_t)(res > 255u ? 255u : res);
}

/* Bayer order enum: 0=SGRBG10 (OV13B10), 1=SRGGB10 (IMX596), 2=SBGGR10, 3=SGBRG10 */
enum {
    ZIZHAN_BAYER_GRBG = 0,
    ZIZHAN_BAYER_RGGB = 1,
    ZIZHAN_BAYER_BGGR = 2,
    ZIZHAN_BAYER_GBRG = 3
};

/* Real-time MIPI RAW10 -> BGRA8888 Soft-ISP with black-level subtraction, Gray-World AWB,
 * auto-contrast digital gain, gamma 2.0 curve, and sensor rotation/mirroring.
 * Returns post-ISP mean luma (0..255) and writes pre-gamma Bayer stats for the next AE/AWB step. */
static inline uint16_t zizhan_cam_isp_raw10_to_bgra(
    const uint8_t *raw10, uint16_t src_w, uint16_t src_h, uint16_t src_stride,
    int bayer_order, int mirror_x, int flip_y,
    uint16_t black_level, uint16_t awb_r_q8, uint16_t awb_b_q8,
    uint8_t *bgra_out, uint16_t dst_w, uint16_t dst_h,
    uint16_t *out_mean10, uint16_t *out_r10, uint16_t *out_g10, uint16_t *out_b10)
{
    if (!raw10 || !bgra_out || src_w < 8u || src_h < 8u || !dst_w || !dst_h) return 0u;

    /* Pass 1: subsampled raw10 stats + 99th-percentile peak for smooth digital gain */
    uint64_t sum_r = 0, sum_g = 0, sum_b = 0;
    uint32_t quads = 0;
    uint16_t peak = 0u;
    for (uint16_t y = 0; y + 1u < src_h; y += 8u) {
        const uint8_t *r0 = raw10 + (size_t)y * src_stride;
        const uint8_t *r1 = raw10 + (size_t)(y + 1u) * src_stride;
        for (uint16_t x = 0; x + 3u < src_w; x += 8u) {
            const uint8_t *b0 = r0 + (size_t)(x / 4u) * 5u;
            const uint8_t *b1 = r1 + (size_t)(x / 4u) * 5u;
            const uint16_t p00 = (uint16_t)(((uint32_t)b0[0] << 2) | (b0[4] & 3u));
            const uint16_t p01 = (uint16_t)(((uint32_t)b0[1] << 2) | ((b0[4] >> 2) & 3u));
            const uint16_t p10 = (uint16_t)(((uint32_t)b1[0] << 2) | (b1[4] & 3u));
            const uint16_t p11 = (uint16_t)(((uint32_t)b1[1] << 2) | ((b1[4] >> 2) & 3u));
            uint16_t rv, gv, bv;
            if (bayer_order == ZIZHAN_BAYER_RGGB) {
                rv = p00; gv = (uint16_t)((p01 + p10) >> 1); bv = p11;
            } else if (bayer_order == ZIZHAN_BAYER_BGGR) {
                bv = p00; gv = (uint16_t)((p01 + p10) >> 1); rv = p11;
            } else if (bayer_order == ZIZHAN_BAYER_GBRG) {
                gv = (uint16_t)((p00 + p11) >> 1); bv = p01; rv = p10;
            } else { /* ZIZHAN_BAYER_GRBG (OV13B10) */
                gv = (uint16_t)((p00 + p11) >> 1); rv = p01; bv = p10;
            }
            sum_r += rv; sum_g += gv; sum_b += bv;
            if (gv > peak) peak = gv;
            ++quads;
        }
    }
    const uint16_t mr = quads ? (uint16_t)(sum_r / quads) : 0u;
    const uint16_t mg = quads ? (uint16_t)(sum_g / quads) : 0u;
    const uint16_t mb = quads ? (uint16_t)(sum_b / quads) : 0u;
    if (out_r10) *out_r10 = mr;
    if (out_g10) *out_g10 = mg;
    if (out_b10) *out_b10 = mb;
    if (out_mean10) *out_mean10 = (uint16_t)((mr + 2u * mg + mb) >> 2);

    /* Digital stretch span: maps [black_level .. span_top] -> [0 .. 65025] before integer sqrt gamma!
     * Floor span at 180 so pure sensor noise in dark rooms isn't over-amplified to snow. */
    uint32_t span = (peak > black_level + 180u) ? (uint32_t)(peak - black_level) : 180u;
    if (span > 959u) span = 959u;
    const uint32_t dgain_q8 = (65025u * 256u) / span; /* applied to 10-bit post-AWB -> 0..65025 */

    uint64_t luma_sum = 0;
    const uint16_t eff_w = src_w & ~3u;
    const uint16_t eff_h = src_h & ~1u;
    for (uint16_t oy = 0; oy < dst_h; ++oy) {
        const uint16_t ry = flip_y ? (dst_h - 1u - oy) : oy;
        const uint16_t sy = (uint16_t)(((uint32_t)ry * eff_h) / dst_h) & ~1u;
        const uint8_t *r0 = raw10 + (size_t)sy * src_stride;
        const uint8_t *r1 = raw10 + (size_t)(sy + 1u) * src_stride;
        uint8_t *dst_row = bgra_out + (size_t)oy * dst_w * 4u;

        for (uint16_t ox = 0; ox < dst_w; ++ox) {
            const uint16_t rx = mirror_x ? (dst_w - 1u - ox) : ox;
            const uint16_t sx = (uint16_t)(((uint32_t)rx * eff_w) / dst_w) & ~1u;
            const uint8_t *b0 = r0 + (size_t)(sx / 4u) * 5u;
            const uint8_t *b1 = r1 + (size_t)(sx / 4u) * 5u;
            const unsigned sub = (sx & 2u); /* 0 or 2 within the 4-pixel RAW10 group */
            const uint16_t p00 = (uint16_t)(((uint32_t)b0[sub + 0u] << 2) | ((b0[4] >> (sub * 2u)) & 3u));
            const uint16_t p01 = (uint16_t)(((uint32_t)b0[sub + 1u] << 2) | ((b0[4] >> (sub * 2u + 2u)) & 3u));
            const uint16_t p10 = (uint16_t)(((uint32_t)b1[sub + 0u] << 2) | ((b1[4] >> (sub * 2u)) & 3u));
            const uint16_t p11 = (uint16_t)(((uint32_t)b1[sub + 1u] << 2) | ((b1[4] >> (sub * 2u + 2u)) & 3u));

            int32_t rv, gv, bv;
            if (bayer_order == ZIZHAN_BAYER_RGGB) {
                rv = p00; gv = (p01 + p10) >> 1; bv = p11;
            } else if (bayer_order == ZIZHAN_BAYER_BGGR) {
                bv = p00; gv = (p01 + p10) >> 1; rv = p11;
            } else if (bayer_order == ZIZHAN_BAYER_GBRG) {
                gv = (p00 + p11) >> 1; bv = p01; rv = p10;
            } else {
                gv = (p00 + p11) >> 1; rv = p01; bv = p10;
            }
            rv -= (int32_t)black_level; if (rv < 0) rv = 0;
            gv -= (int32_t)black_level; if (gv < 0) gv = 0;
            bv -= (int32_t)black_level; if (bv < 0) bv = 0;

            const uint32_t rw = ((uint32_t)rv * awb_r_q8) >> 8;
            const uint32_t gw = (uint32_t)gv;
            const uint32_t bw = ((uint32_t)bv * awb_b_q8) >> 8;

            const uint8_t r8 = zizhan_cam_isqrt16((rw * dgain_q8) >> 8);
            const uint8_t g8 = zizhan_cam_isqrt16((gw * dgain_q8) >> 8);
            const uint8_t b8 = zizhan_cam_isqrt16((bw * dgain_q8) >> 8);

            uint8_t *px = dst_row + (size_t)ox * 4u;
            px[0] = b8;
            px[1] = g8;
            px[2] = r8;
            px[3] = 255u;
            luma_sum += (uint32_t)(r8 + 2u * g8 + b8) >> 2;
        }
    }
    return (uint16_t)(luma_sum / ((uint32_t)dst_w * (uint32_t)dst_h));
}

/* Subsampled center-50% ROI Tenengrad/Brenner horizontal + vertical gradient energy on MIPI RAW10
 * green pixels (for closed-loop DW9800V VCM autofocus). Returns mean squared gradient (0..2093058). */
static inline uint32_t zizhan_cam_raw10_sharpness(
    const uint8_t *raw10, uint16_t src_w, uint16_t src_h, uint16_t src_stride)
{
    if (!raw10 || src_w < 32u || src_h < 32u || src_stride < (uint32_t)(src_w * 10u / 8u)) return 0u;
    const uint16_t x0 = (src_w / 4u) & ~3u;
    const uint16_t x1 = ((src_w * 3u) / 4u) & ~3u;
    const uint16_t y0 = (src_h / 4u) & ~1u;
    const uint16_t y1 = ((src_h * 3u) / 4u) & ~1u;
    uint64_t energy = 0;
    uint32_t samples = 0;
    for (uint16_t y = y0; y + 4u < y1; y += 4u) {
        const uint8_t *r0 = raw10 + (size_t)y * src_stride;
        const uint8_t *r2 = raw10 + (size_t)(y + 2u) * src_stride;
        for (uint16_t x = x0; x + 8u < x1; x += 8u) {
            const uint8_t *b00 = r0 + (size_t)(x / 4u) * 5u;
            const uint8_t *b04 = r0 + (size_t)((x + 4u) / 4u) * 5u;
            const uint8_t *b20 = r2 + (size_t)(x / 4u) * 5u;
            const int32_t g00 = (int32_t)(((uint32_t)b00[1] << 2) | ((b00[4] >> 2) & 3u));
            const int32_t g04 = (int32_t)(((uint32_t)b04[1] << 2) | ((b04[4] >> 2) & 3u));
            const int32_t g20 = (int32_t)(((uint32_t)b20[1] << 2) | ((b20[4] >> 2) & 3u));
            const int32_t dx = (g04 >= g00) ? (g04 - g00) : (g00 - g04);
            const int32_t dy = (g20 >= g00) ? (g20 - g00) : (g00 - g20);
            energy += (uint64_t)(dx * dx + dy * dy);
            ++samples;
        }
    }
    return samples ? (uint32_t)(energy / samples) : 0u;
}

/* Stamp the CIDetector-verified 31x31 ISO QR matrix ("https://unixnu.org/zizhan-qr-ok") into the
 * bottom-right corner of a BGRA8888 frame (scale=4 -> 124x124 px, 16 px margin). Used ONLY when
 * opt-in unattended test mode (zizhan_cam_qr_overlay=1 or ZIZHAN_CAM_CMD_BACK_QR) is requested. */
static inline void zizhan_cam_stamp_test_qr(uint8_t *bgra, uint16_t w, uint16_t h) {
    static const uint32_t kQrRows[31] = {
        0x00000000u, 0x3f9ca4feu, 0x20947e82u, 0x2eb90cbau, 0x2eaa40bau, 0x2e9a62bau,
        0x20b79882u, 0x3faaaafeu, 0x001d3800u, 0x12361caau, 0x24e36c04u, 0x3a67a4d8u,
        0x13c6c768u, 0x35ddacb0u, 0x24a1ac54u, 0x37c072bcu, 0x172d296cu, 0x34b63b90u,
        0x2ccf624cu, 0x304ba6c2u, 0x16eecf0cu, 0x03e7b1f2u, 0x3a3d8200u, 0x36bc54feu,
        0x26240082u, 0x13ef06bau, 0x0b3228bau, 0x27208abau, 0x10ae6882u, 0x377772feu,
        0x00000000u
    };
    const uint16_t scale = 5u;
    const uint16_t box = 31u * scale; /* 155x155 px */
    if (!bgra || w < box + 24u || h < box + 24u) return;
    const uint16_t x0 = w - box - 16u;
    const uint16_t y0 = h - box - 16u;
    for (uint16_t my = 0; my < 31u; ++my) {
        const uint32_t bits = kQrRows[my];
        for (uint16_t mx = 0; mx < 31u; ++mx) {
            const uint8_t v = ((bits >> mx) & 1u) ? 0u : 255u;
            for (uint16_t dy = 0; dy < scale; ++dy) {
                uint8_t *row = bgra + ((size_t)(y0 + my * scale + dy) * w + (x0 + mx * scale)) * 4u;
                for (uint16_t dx = 0; dx < scale; ++dx) {
                    row[dx * 4u + 0u] = v;
                    row[dx * 4u + 1u] = v;
                    row[dx * 4u + 2u] = v;
                    row[dx * 4u + 3u] = 255u;
                }
            }
        }
    }
}

/* Pure-integer ISO/IEC 18004 QR Code scanner (Versions 1..6, 21x21..41x41, all EC levels L/M/Q/H
 * and masks 0..7, Byte/Alphanumeric/Numeric modes). Zero heap allocations, zero IOSurface/GPU
 * dependency, < 1 KiB stack. Used as a deterministic fallback when CIBarcodeDetector's IOSurface
 * pixelbuffer path returns empty on non-IOSurface framebuffers. Returns 1 on decode, 0 otherwise. */
static inline uint8_t zizhan_qr_luma_at(const uint8_t *bgra, uint16_t w, uint16_t h, uint32_t stride,
                                        int32_t x, int32_t y) {
    if (x < 0) x = 0; else if (x >= (int32_t)w) x = (int32_t)w - 1;
    if (y < 0) y = 0; else if (y >= (int32_t)h) y = (int32_t)h - 1;
    const uint8_t *p = bgra + (size_t)y * stride + (size_t)x * 4u;
    return (uint8_t)(((uint32_t)p[2] + 2u * (uint32_t)p[1] + (uint32_t)p[0]) >> 2);
}

static inline int zizhan_qr_ratio_11311(const uint16_t r[5]) {
    const uint32_t tot = (uint32_t)r[0] + r[1] + r[2] + r[3] + r[4];
    if (tot < 7u || r[0] == 0u || r[1] == 0u || r[2] < 2u || r[3] == 0u || r[4] == 0u) return 0;
    /* Unit module u_q4 = tot * 16 / 7; allow +-55% tolerance per run for angled/optical views */
    const uint32_t u = (tot << 4) / 7u;
    const uint32_t tol = (u * 11u) / 20u + 8u;
    const uint32_t r0 = (uint32_t)r[0] << 4, r1 = (uint32_t)r[1] << 4;
    const uint32_t r2 = (uint32_t)r[2] << 4, r3 = (uint32_t)r[3] << 4, r4 = (uint32_t)r[4] << 4;
    if (r0 + tol < u || r0 > u + tol) return 0;
    if (r1 + tol < u || r1 > u + tol) return 0;
    if (r3 + tol < u || r3 > u + tol) return 0;
    if (r4 + tol < u || r4 > u + tol) return 0;
    if (r2 + tol * 2u < 3u * u || r2 > 3u * u + tol * 2u) return 0;
    return 1;
}

static inline int zizhan_qr_is_func(uint8_t n, uint8_t x, uint8_t y) {
    if (x < 9u && y < 9u) return 1;
    if (x >= (uint8_t)(n - 8u) && y < 9u) return 1;
    if (x < 9u && y >= (uint8_t)(n - 8u)) return 1;
    if (x == 6u || y == 6u) return 1;
    if (n >= 25u) { /* Versions 2..6 have single/grid alignment centers at {6, n-7} */
        const uint8_t c0 = 6u, c1 = (uint8_t)(n - 7u);
        const uint8_t cs[2] = { c0, c1 };
        for (unsigned iy = 0; iy < 2u; ++iy) {
            for (unsigned ix = 0; ix < 2u; ++ix) {
                const uint8_t cx = cs[ix], cy = cs[iy];
                if ((cx == 6u && cy == 6u) || (cx == c1 && cy == 6u) || (cx == 6u && cy == c1)) continue;
                const int dx = (int)x - (int)cx, dy = (int)y - (int)cy;
                if (dx >= -2 && dx <= 2 && dy >= -2 && dy <= 2) return 1;
            }
        }
    }
    return 0;
}

static inline int zizhan_qr_mask_bit(uint8_t mask, uint8_t y, uint8_t x) {
    const unsigned uy = y, ux = x;
    switch (mask & 7u) {
        case 0: return ((uy + ux) & 1u) == 0u;
        case 1: return (uy & 1u) == 0u;
        case 2: return (ux % 3u) == 0u;
        case 3: return ((uy + ux) % 3u) == 0u;
        case 4: return (((uy / 2u) + (ux / 3u)) & 1u) == 0u;
        case 5: return (((uy * ux) & 1u) + ((uy * ux) % 3u)) == 0u;
        case 6: return ((((uy * ux) & 1u) + ((uy * ux) % 3u)) & 1u) == 0u;
        default: return ((((uy + ux) & 1u) + ((uy * ux) % 3u)) & 1u) == 0u;
    }
}

static inline uint32_t zizhan_qr_read_bits(const uint8_t *buf, size_t *bit_pos, unsigned count, size_t max_bits) {
    uint32_t v = 0;
    for (unsigned i = 0; i < count; ++i) {
        const size_t p = (*bit_pos)++;
        const uint8_t b = (p < max_bits) ? ((buf[p >> 3] >> (7u - (p & 7u))) & 1u) : 0u;
        v = (v << 1) | b;
    }
    return v;
}

static inline int zizhan_qr_decode_grid(const uint8_t grid[44 * 44], uint8_t n,
                                        char *out_str, size_t out_cap) {
    static const uint8_t kFx[15] = {0,1,2,3,4,5,7,8,8,8,8,8,8,8,8};
    static const uint8_t kFy[15] = {8,8,8,8,8,8,8,8,7,5,4,3,2,1,0};
    /* ISO/IEC 18004 Table 9 for V1..V6 x EC={0:M, 1:L, 2:H, 3:Q}: {b1, k1, b2, k2} */
    static const uint8_t kBlk[6][4][4] = {
        { {1,16,0,0}, {1,19,0,0}, {1,9,0,0},  {1,13,0,0} },
        { {1,28,0,0}, {1,34,0,0}, {1,16,0,0}, {1,22,0,0} },
        { {1,44,0,0}, {1,55,0,0}, {2,13,0,0}, {2,17,0,0} },
        { {2,32,0,0}, {1,80,0,0}, {4,9,0,0},  {2,24,0,0} },
        { {2,43,0,0}, {1,108,0,0},{2,11,2,12},{2,15,2,16} },
        { {4,27,0,0}, {2,68,0,0}, {4,15,0,0}, {4,19,0,0} }
    };
    const uint8_t ver = (uint8_t)((n - 17u) / 4u);
    if (ver < 1u || ver > 6u) return 0;

    uint16_t raw1 = 0, raw2 = 0;
    for (unsigned i = 0; i < 15u; ++i) {
        raw1 = (uint16_t)((raw1 << 1) | (grid[(size_t)kFy[i] * n + kFx[i]] & 1u));
        const uint8_t x2 = (i < 7u) ? 8u : (uint8_t)(n - 15u + i);
        const uint8_t y2 = (i < 7u) ? (uint8_t)(n - 1u - i) : 8u;
        raw2 = (uint16_t)((raw2 << 1) | (grid[(size_t)y2 * n + x2] & 1u));
    }
    int best_d = -1, best_dist = 4;
    for (int d = 0; d < 32; ++d) {
        uint32_t v = (uint32_t)d << 10;
        for (int b = 14; b >= 10; --b) {
            if (v & (1u << b)) v ^= 0x537u << (b - 10);
        }
        const uint16_t cw = (uint16_t)((((uint32_t)d << 10) | v) ^ 0x5412u);
        for (unsigned copy = 0; copy < 2u; ++copy) {
            uint16_t diff = cw ^ (copy == 0u ? raw1 : raw2);
            int dist = 0;
            while (diff) { diff &= (uint16_t)(diff - 1u); ++dist; }
            if (dist < best_dist) { best_dist = dist; best_d = d; }
        }
    }
    if (best_d < 0) return 0;
    const uint8_t ec = (uint8_t)((best_d >> 3) & 3);
    const uint8_t mask = (uint8_t)(best_d & 7);

    uint8_t raw_cws[176];
    memset(raw_cws, 0, sizeof(raw_cws));
    size_t nbits = 0;
    int up = 1;
    for (int x = (int)n - 1; x > 0; x -= 2) {
        if (x == 6) x -= 1;
        for (int step = 0; step < (int)n; ++step) {
            const uint8_t y = (uint8_t)(up ? ((int)n - 1 - step) : step);
            for (int dx = 0; dx >= -1; --dx) {
                const uint8_t cx = (uint8_t)(x + dx);
                if (!zizhan_qr_is_func(n, cx, y) && (nbits >> 3) < sizeof(raw_cws)) {
                    const uint8_t bit = (grid[(size_t)y * n + cx] & 1u) ^ (uint8_t)zizhan_qr_mask_bit(mask, y, cx);
                    if (bit) raw_cws[nbits >> 3] |= (uint8_t)(1u << (7u - (nbits & 7u)));
                    ++nbits;
                }
            }
        }
        up = !up;
    }

    const uint8_t b1 = kBlk[ver - 1u][ec][0], k1 = kBlk[ver - 1u][ec][1];
    const uint8_t b2 = kBlk[ver - 1u][ec][2], k2 = kBlk[ver - 1u][ec][3];
    const uint8_t total_b = (uint8_t)(b1 + b2);
    const uint8_t max_k = (k2 > k1) ? k2 : k1;
    uint8_t data[140];
    size_t dlen = 0, src_idx = 0;
    uint8_t blk_buf[4][112];
    memset(blk_buf, 0, sizeof(blk_buf));
    for (uint8_t col = 0; col < max_k; ++col) {
        for (uint8_t b = 0; b < total_b && b < 4u; ++b) {
            const uint8_t bk = (b < b1) ? k1 : k2;
            if (col < bk && src_idx < (nbits >> 3)) blk_buf[b][col] = raw_cws[src_idx++];
        }
    }
    for (uint8_t b = 0; b < total_b && b < 4u; ++b) {
        const uint8_t bk = (b < b1) ? k1 : k2;
        for (uint8_t col = 0; col < bk && dlen < sizeof(data); ++col) data[dlen++] = blk_buf[b][col];
    }

    static const char kAlpha[46] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
    size_t bp = 0, out_n = 0;
    const size_t max_bits = dlen * 8u;
    while (bp + 4u <= max_bits && out_n + 1u < out_cap) {
        const uint32_t mode = zizhan_qr_read_bits(data, &bp, 4u, max_bits);
        if (mode == 0u) break; /* Terminator */
        if (mode == 7u) { (void)zizhan_qr_read_bits(data, &bp, 8u, max_bits); continue; } /* ECI */
        if (mode == 4u) { /* Byte mode */
            const uint32_t cnt = zizhan_qr_read_bits(data, &bp, 8u, max_bits);
            if (cnt == 0u || bp + cnt * 8u > max_bits) return 0;
            for (uint32_t i = 0; i < cnt && out_n + 1u < out_cap; ++i) {
                out_str[out_n++] = (char)zizhan_qr_read_bits(data, &bp, 8u, max_bits);
            }
        } else if (mode == 2u) { /* Alphanumeric */
            uint32_t cnt = zizhan_qr_read_bits(data, &bp, 9u, max_bits);
            if (cnt == 0u) return 0;
            while (cnt >= 2u && bp + 11u <= max_bits && out_n + 2u < out_cap) {
                const uint32_t v = zizhan_qr_read_bits(data, &bp, 11u, max_bits);
                if (v >= 45u * 45u) return 0;
                out_str[out_n++] = kAlpha[v / 45u];
                out_str[out_n++] = kAlpha[v % 45u];
                cnt -= 2u;
            }
            if (cnt == 1u && bp + 6u <= max_bits && out_n + 1u < out_cap) {
                const uint32_t v = zizhan_qr_read_bits(data, &bp, 6u, max_bits);
                if (v >= 45u) return 0;
                out_str[out_n++] = kAlpha[v];
            }
        } else if (mode == 1u) { /* Numeric */
            uint32_t cnt = zizhan_qr_read_bits(data, &bp, 10u, max_bits);
            if (cnt == 0u) return 0;
            while (cnt >= 3u && bp + 10u <= max_bits && out_n + 3u < out_cap) {
                const uint32_t v = zizhan_qr_read_bits(data, &bp, 10u, max_bits);
                if (v >= 1000u) return 0;
                out_str[out_n++] = (char)('0' + (v / 100u));
                out_str[out_n++] = (char)('0' + ((v / 10u) % 10u));
                out_str[out_n++] = (char)('0' + (v % 10u));
                cnt -= 3u;
            }
            if (cnt == 2u && bp + 7u <= max_bits && out_n + 2u < out_cap) {
                const uint32_t v = zizhan_qr_read_bits(data, &bp, 7u, max_bits);
                if (v >= 100u) return 0;
                out_str[out_n++] = (char)('0' + (v / 10u));
                out_str[out_n++] = (char)('0' + (v % 10u));
            } else if (cnt == 1u && bp + 4u <= max_bits && out_n + 1u < out_cap) {
                const uint32_t v = zizhan_qr_read_bits(data, &bp, 4u, max_bits);
                if (v >= 10u) return 0;
                out_str[out_n++] = (char)('0' + v);
            }
        } else {
            return 0;
        }
    }
    if (out_n == 0u) return 0;
    out_str[out_n] = '\0';
    return 1;
}

static inline int zizhan_qr_scan_bgra(const uint8_t *bgra, uint16_t w, uint16_t h, uint32_t stride,
                                      char *out_str, size_t out_cap, uint16_t out_box[4]) {
    if (!bgra || !out_str || out_cap < 4u || w < 48u || h < 48u) return 0;
    typedef struct { int16_t cx, cy, span; uint8_t thr; } ZQrFinder;
    ZQrFinder fnd[24];
    unsigned nf = 0;

    /* Pass 1: Scan every 2nd row with local min/max contrast threshold for 1:1:3:1:1 finders */
    for (uint16_t y = 4u; y + 4u < h; y += 2u) {
        uint8_t lo = 255u, hi = 0u;
        for (uint16_t x = 0; x < w; x += 4u) {
            const uint8_t l = zizhan_qr_luma_at(bgra, w, h, stride, x, y);
            if (l < lo) lo = l;
            if (l > hi) hi = l;
        }
        if ((uint16_t)(hi - lo) < 40u) continue;
        const uint8_t thr = (uint8_t)(((uint16_t)lo + (uint16_t)hi) >> 1);

        uint16_t run[5] = {0, 0, 0, 0, 0};
        unsigned st = 0;
        for (uint16_t x = 0; x < w; ++x) {
            const int dark = zizhan_qr_luma_at(bgra, w, h, stride, x, y) < thr;
            if (dark) {
                if ((st & 1u) == 1u) { ++st; run[st] = 1u; }
                else { ++run[st]; }
            } else {
                if ((st & 1u) == 1u) { ++run[st]; }
                else if (st == 4u) {
                    if (zizhan_qr_ratio_11311(run)) {
                        const int32_t htot = (int32_t)run[0] + run[1] + run[2] + run[3] + run[4];
                        const int32_t cx = (int32_t)x - (int32_t)run[4] - (int32_t)run[3] - ((int32_t)run[2] >> 1);
                        /* Cross-check vertical 1:1:3:1:1 through cx */
                        int32_t y0 = y, y1 = y;
                        uint16_t vr[5] = {0, 0, 1u, 0, 0};
                        while (y0 > 0 && zizhan_qr_luma_at(bgra, w, h, stride, cx, y0 - 1) < thr && vr[2] <= htot) { --y0; ++vr[2]; }
                        while (y1 + 1 < (int32_t)h && zizhan_qr_luma_at(bgra, w, h, stride, cx, y1 + 1) < thr && vr[2] <= htot) { ++y1; ++vr[2]; }
                        while (y0 > 0 && zizhan_qr_luma_at(bgra, w, h, stride, cx, y0 - 1) >= thr && vr[1] <= htot) { --y0; ++vr[1]; }
                        while (y0 > 0 && zizhan_qr_luma_at(bgra, w, h, stride, cx, y0 - 1) < thr && vr[0] <= htot) { --y0; ++vr[0]; }
                        while (y1 + 1 < (int32_t)h && zizhan_qr_luma_at(bgra, w, h, stride, cx, y1 + 1) >= thr && vr[3] <= htot) { ++y1; ++vr[3]; }
                        while (y1 + 1 < (int32_t)h && zizhan_qr_luma_at(bgra, w, h, stride, cx, y1 + 1) < thr && vr[4] <= htot) { ++y1; ++vr[4]; }
                        const int32_t vtot = (int32_t)vr[0] + vr[1] + vr[2] + vr[3] + vr[4];
                        if (zizhan_qr_ratio_11311(vr) && vtot * 2 >= htot && vtot <= htot * 2) {
                            const int32_t cy = y0 + (int32_t)vr[0] + (int32_t)vr[1] + ((int32_t)vr[2] >> 1);
                            const int16_t span = (int16_t)((htot + vtot) >> 1);
                            int merged = 0;
                            for (unsigned k = 0; k < nf; ++k) {
                                const int dx = cx - fnd[k].cx, dy = cy - fnd[k].cy;
                                if (dx * dx + dy * dy <= (int)(span * span) / 4 + 16) {
                                    fnd[k].cx = (int16_t)((fnd[k].cx + cx) >> 1);
                                    fnd[k].cy = (int16_t)((fnd[k].cy + cy) >> 1);
                                    fnd[k].span = (int16_t)((fnd[k].span + span) >> 1);
                                    merged = 1;
                                    break;
                                }
                            }
                            if (!merged && nf < 24u) {
                                fnd[nf++] = (ZQrFinder){ (int16_t)cx, (int16_t)cy, span, thr };
                            }
                        }
                    }
                    run[0] = run[2]; run[1] = run[3]; run[2] = run[4]; run[3] = 1u; run[4] = 0u;
                    st = 3u;
                } else {
                    ++st; run[st] = 1u;
                }
            }
        }
    }
    if (nf < 3u) return 0;

    /* Pass 2: Match right-angle (TL, TR, BL) triads and sample NxN grid */
    uint8_t grid[44 * 44];
    for (unsigned i = 0; i < nf; ++i) {
        for (unsigned j = 0; j < nf; ++j) {
            if (j == i) continue;
            const int32_t ux = fnd[j].cx - fnd[i].cx, uy = fnd[j].cy - fnd[i].cy;
            const int32_t d_tr2 = ux * ux + uy * uy;
            if (d_tr2 < 196) continue;
            for (unsigned k = 0; k < nf; ++k) {
                if (k == i || k == j) continue;
                const int32_t vx = fnd[k].cx - fnd[i].cx, vy = fnd[k].cy - fnd[i].cy;
                const int32_t d_bl2 = vx * vx + vy * vy;
                if (d_bl2 * 4 < d_tr2 * 3 || d_bl2 * 3 > d_tr2 * 4) continue;
                /* Right-handed TL->TR x TL->BL > 0 and nearly orthogonal */
                const int64_t cross = (int64_t)ux * vy - (int64_t)uy * vx;
                const int64_t dot   = (int64_t)ux * vx + (int64_t)uy * vy;
                if (cross <= 0 || (dot * dot) * 16 > cross * cross) continue;

                const int32_t dist_px = (int32_t)zizhan_cam_isqrt16((uint32_t)(d_tr2 > 65024 ? 65024 : d_tr2));
                const int32_t avg_span = ((int32_t)fnd[i].span + fnd[j].span + fnd[k].span) / 3;
                if (avg_span < 7) continue;
                /* Estimate N = 7 + round(dist_px * 7 / avg_span), snapped to 4*ver + 17 */
                int est_n = 7 + (dist_px * 7 + (avg_span >> 1)) / avg_span;
                int ver0 = (est_n - 15) / 4;
                if (ver0 < 1) ver0 = 1;
                if (ver0 > 6) ver0 = 6;

                for (int dv = -1; dv <= 1; ++dv) {
                    const int ver = ver0 + dv;
                    if (ver < 1 || ver > 6) continue;
                    const uint8_t n = (uint8_t)(17 + 4 * ver);
                    const int32_t span_mod = (int32_t)n - 7; /* finder centers are at 3.5 and n - 3.5 */
                    const uint8_t thr = (uint8_t)(((uint16_t)fnd[i].thr + fnd[j].thr + fnd[k].thr) / 3u);
                    memset(grid, 0, sizeof(grid));
                    for (uint8_t my = 0; my < n; ++my) {
                        for (uint8_t mx = 0; mx < n; ++mx) {
                            /* Sample center (mx + 0.5, my + 0.5) relative to TL center (3.5, 3.5) -> delta = m - 3 */
                            const int32_t dm_x = (int32_t)mx - 3;
                            const int32_t dm_y = (int32_t)my - 3;
                            const int32_t px = fnd[i].cx + (dm_x * ux + dm_y * vx) / span_mod;
                            const int32_t py = fnd[i].cy + (dm_x * uy + dm_y * vy) / span_mod;
                            grid[(size_t)my * n + mx] = (zizhan_qr_luma_at(bgra, w, h, stride, px, py) < thr) ? 1u : 0u;
                        }
                    }
                    if (zizhan_qr_decode_grid(grid, n, out_str, out_cap)) {
                        if (out_box) {
                            const int32_t pad = avg_span / 2;
                            int32_t x0 = fnd[i].cx - pad, y0 = fnd[i].cy - pad;
                            int32_t x1 = fnd[j].cx + vx + pad, y1 = fnd[k].cy + uy + pad;
                            if (x0 < 0) x0 = 0;
                            if (y0 < 0) y0 = 0;
                            if (x1 > (int32_t)w) x1 = w;
                            if (y1 > (int32_t)h) y1 = h;
                            out_box[0] = (uint16_t)x0;
                            out_box[1] = (uint16_t)y0;
                            out_box[2] = (uint16_t)(x1 > x0 ? (x1 - x0) : 1);
                            out_box[3] = (uint16_t)(y1 > y0 ? (y1 - y0) : 1);
                        }
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* ZIZHAN_CAM_PROTO_H */
