/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "../../../userland/zizhan-cam-proto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void pack_raw10_quad(uint8_t *raw, uint16_t stride, uint16_t x, uint16_t y,
                            uint16_t p00, uint16_t p01, uint16_t p10, uint16_t p11) {
    uint8_t *r0 = raw + (size_t)y * stride + (size_t)(x / 4u) * 5u;
    uint8_t *r1 = raw + (size_t)(y + 1u) * stride + (size_t)(x / 4u) * 5u;
    const unsigned sub = x & 2u;
    r0[sub + 0u] = (uint8_t)(p00 >> 2);
    r0[sub + 1u] = (uint8_t)(p01 >> 2);
    r0[4u] = (uint8_t)((r0[4u] & ~(0xfu << (sub * 2u))) |
                       (((p00 & 3u) | ((p01 & 3u) << 2)) << (sub * 2u)));
    r1[sub + 0u] = (uint8_t)(p10 >> 2);
    r1[sub + 1u] = (uint8_t)(p11 >> 2);
    r1[4u] = (uint8_t)((r1[4u] & ~(0xfu << (sub * 2u))) |
                       (((p10 & 3u) | ((p11 & 3u) << 2)) << (sub * 2u)));
}

static void test_ae_closed_loop(void) {
    ZizhanCamAeState ae;
    zizhan_cam_ae_init(&ae, 400u, 1500u, 256u, 3840u);

    /* Dark scene (mean10 = 80, signal = 16 vs target 160): AE increases exposure lines first */
    for (int i = 0; i < 6; ++i) {
        (void)zizhan_cam_ae_step(&ae, 80u);
    }
    assert(ae.exposure_lines > 1000u);

    /* Very dark scene saturates exposure at max_lines=1500, then increases analog gain > 1.0x */
    for (int i = 0; i < 6; ++i) {
        (void)zizhan_cam_ae_step(&ae, 75u);
    }
    assert(ae.exposure_lines == 1500u);
    assert(ae.gain_q8 > 512u);

    /* Within +-15% deadband of target (64 + 160 = 224): AE returns 0 (no hunting) */
    const uint32_t hold_lines = ae.exposure_lines, hold_gain = ae.gain_q8;
    assert(zizhan_cam_ae_step(&ae, 224u) == 0);
    assert(ae.exposure_lines == hold_lines && ae.gain_q8 == hold_gain);

    /* Overexposed scene (mean10 = 900): AE steps down gain & exposure */
    assert(zizhan_cam_ae_step(&ae, 900u) == 1);
    assert((uint64_t)ae.exposure_lines * ae.gain_q8 < (uint64_t)hold_lines * hold_gain);
}

static void test_awb_and_isp_bgra(void) {
    const uint16_t sw = 320u, sh = 240u, stride = 400u;
    uint8_t *raw = (uint8_t *)calloc(1, (size_t)stride * sh);
    uint8_t *bgra = (uint8_t *)calloc(1, 160u * 120u * 4u);
    assert(raw && bgra);

    /* Warm indoor tungsten cast on GRBG Bayer: R=364, G=264, B=164 (above black=64 -> R=300, G=200, B=100) */
    for (uint16_t y = 0; y < sh; y += 2u) {
        for (uint16_t x = 0; x < sw; x += 2u) {
            pack_raw10_quad(raw, stride, x, y, 264u, 364u, 164u, 264u);
        }
    }

    uint16_t mean10 = 0, r10 = 0, g10 = 0, b10 = 0;
    (void)zizhan_cam_isp_raw10_to_bgra(raw, sw, sh, stride, ZIZHAN_BAYER_GRBG, 0, 0,
                                       64u, 256u, 256u, bgra, 160u, 120u,
                                       &mean10, &r10, &g10, &b10);
    assert(r10 == 364u && g10 == 264u && b10 == 164u);

    uint16_t rq = 256u, bq = 256u;
    zizhan_cam_awb_gains(r10, g10, b10, 64u, &rq, &bq);
    /* Expected: rq = 200*256/300 = 170, bq = 200*256/100 = 460 (clamped) */
    assert(rq >= 168u && rq <= 172u);
    assert(bq == 460u);

    const uint16_t luma = zizhan_cam_isp_raw10_to_bgra(
        raw, sw, sh, stride, ZIZHAN_BAYER_GRBG, 0, 0,
        64u, rq, bq, bgra, 160u, 120u, &mean10, NULL, NULL, NULL);
    assert(luma > 100u);
    /* Neutralized pixel: R andG should now be within 2 levels of each other */
    const int diff_rg = (int)bgra[2] - (int)bgra[1];
    assert(diff_rg >= -3 && diff_rg <= 3 && bgra[3] == 255u);

    /* Also test QR stamp bounds + pure-C ISO-18004 QR scanner on noisy optical 960x720 BGRA frame */
    uint8_t *f960 = (uint8_t *)calloc(1, ZIZHAN_CAM_OUT_BYTES);
    assert(f960 != NULL);
    for (size_t i = 0; i < ZIZHAN_CAM_OUT_BYTES; i += 4u) {
        f960[i + 0u] = (uint8_t)(40u + ((i * 17u) & 63u));
        f960[i + 1u] = (uint8_t)(60u + ((i * 31u) & 63u));
        f960[i + 2u] = (uint8_t)(50u + ((i * 13u) & 63u));
        f960[i + 3u] = 255u;
    }
    zizhan_cam_stamp_test_qr(f960, ZIZHAN_CAM_OUT_W, ZIZHAN_CAM_OUT_H);
    /* Bottom-right quiet zone pixel (W-17, H-17) must be white (255,255,255,255) */
    const uint8_t *qz = f960 + ((size_t)(ZIZHAN_CAM_OUT_H - 17u) * ZIZHAN_CAM_OUT_W + (ZIZHAN_CAM_OUT_W - 17u)) * 4u;
    assert(qz[0] == 255u && qz[1] == 255u && qz[2] == 255u && qz[3] == 255u);

    char qr_msg[128] = {0};
    uint16_t qr_box[4] = {0};
    const int qr_ok = zizhan_qr_scan_bgra(f960, ZIZHAN_CAM_OUT_W, ZIZHAN_CAM_OUT_H,
                                          ZIZHAN_CAM_OUT_STRIDE, qr_msg, sizeof(qr_msg), qr_box);
    assert(qr_ok == 1);
    assert(strcmp(qr_msg, "https://unixnu.org/zizhan-qr-ok") == 0);
    assert(qr_box[0] >= 740u && qr_box[1] >= 520u && qr_box[2] >= 120u && qr_box[3] >= 120u);

    /* Test RAW10 Tenengrad AF sharpness metric: sharp high-frequency checkerboard >> flat blurred frame */
    memset(raw, 0, (size_t)stride * sh);
    for (uint16_t y = 0; y < sh; y += 2u) {
        for (uint16_t x = 0; x < sw; x += 2u) {
            pack_raw10_quad(raw, stride, x, y, 250u, 252u, 248u, 250u);
        }
    }
    const uint32_t blur_sh = zizhan_cam_raw10_sharpness(raw, sw, sh, stride);
    for (uint16_t y = 0; y < sh; y += 2u) {
        for (uint16_t x = 0; x < sw; x += 2u) {
            const uint16_t hi = (((x >> 2) ^ (y >> 1)) & 1u) ? 780u : 120u;
            pack_raw10_quad(raw, stride, x, y, hi, hi, hi, hi);
        }
    }
    const uint32_t sharp_sh = zizhan_cam_raw10_sharpness(raw, sw, sh, stride);
    assert(sharp_sh > 100000u && sharp_sh > blur_sh * 100u);

    free(f960);
    free(bgra);
    free(raw);
}

static void test_cam04_orientation_bayer_hotpixel_and_denoise(void) {
    /* 1. Verify 90 deg CW rear mount rotation + native Bayer parity invariance:
     *    Raw sensor is 160x120 (even). Under 90 deg CW rotation -> 120x160 upright portrait:
     *    - Raw bottom-left (x=0..18, y=100..118) moves to output TOP-LEFT (ox=4, oy=4)!
     *    - Raw top-left (x=0..18, y=0..18) moves to output TOP-RIGHT (ox=115, oy=4)!
     *    Pack pure RED (R=700, G=100, B=100) at raw bottom-left and pure BLUE (R=100, G=100, B=700)
     *    at raw top-left in GRBG order, and verify NO R/B channel swap occurs after 90 deg CW! */
    const uint16_t sw = 160u, sh = 120u, stride = 200u;
    uint8_t *raw = (uint8_t *)calloc(1, (size_t)stride * sh);
    uint8_t *out_rear = (uint8_t *)calloc(1, 120u * 160u * 4u);
    assert(raw && out_rear);
    for (uint16_t y = 0; y < sh; y += 2u) {
        for (uint16_t x = 0; x < sw; x += 2u) {
            uint16_t r = 240u, g = 240u, b = 240u;
            if (x < 24u && y >= 96u) {
                r = 720u; g = 110u; b = 110u; /* raw bottom-left = RED */
            } else if (x < 24u && y < 24u) {
                r = 110u; g = 110u; b = 720u; /* raw top-left = BLUE */
            }
            /* GRBG: p00=G, p01=R, p10=B, p11=G */
            pack_raw10_quad(raw, stride, x, y, g, r, b, g);
        }
    }
    uint32_t dgain = 0;
    (void)zizhan_cam_isp_raw10_to_bgra_ex(
        raw, sw, sh, stride, ZIZHAN_BAYER_GRBG, 90, 0, 0,
        64u, 256u, 256u, 256u, 1, &dgain, out_rear, 120u, 160u,
        NULL, NULL, NULL, NULL);
    const uint8_t *tl = out_rear + (4u * 120u + 4u) * 4u;
    const uint8_t *tr = out_rear + (4u * 120u + 115u) * 4u;
    const uint8_t *mid = out_rear + (80u * 120u + 60u) * 4u;
    /* BGRA layout: [0]=B, [1]=G, [2]=R, [3]=A */
    assert(tl[2] > 200u && tl[2] > tl[0] + 100u && tl[2] > tl[1] + 80u); /* Top-left is RED */
    assert(tr[0] > 200u && tr[0] > tr[2] + 100u && tr[0] > tr[1] + 80u); /* Top-right is BLUE */
    /* CCM neutral-gray invariance: mid gray (240,240,240) stays strictly R==G==B! */
    assert(abs((int)mid[0] - (int)mid[1]) <= 1 && abs((int)mid[2] - (int)mid[1]) <= 1);

    /* 2. Verify 270 deg CW + horizontal selfie mirror on RGGB (IMX596 front camera):
     *    Raw bottom-right (x=136..158, y=96..118) -> unmirrored 270 CW top-right -> mirrored TOP-LEFT! */
    memset(raw, 0, (size_t)stride * sh);
    for (uint16_t y = 0; y < sh; y += 2u) {
        for (uint16_t x = 0; x < sw; x += 2u) {
            uint16_t r = 220u, g = 220u, b = 220u;
            if (x >= 136u && y >= 96u) {
                r = 120u; g = 740u; b = 120u; /* raw bottom-right = GREEN */
            }
            /* RGGB: p00=R, p01=G, p10=G, p11=B */
            pack_raw10_quad(raw, stride, x, y, r, g, g, b);
        }
    }
    dgain = 0;
    (void)zizhan_cam_isp_raw10_to_bgra_ex(
        raw, sw, sh, stride, ZIZHAN_BAYER_RGGB, 270, 1, 0,
        64u, 256u, 256u, 256u, 1, &dgain, out_rear, 120u, 160u,
        NULL, NULL, NULL, NULL);
    assert(tl[1] > 200u && tl[1] > tl[0] + 80u && tl[1] > tl[2] + 80u); /* Mirrored top-left is GREEN */

    /* 3. Hot-pixel immunity + high-gain dark-frame noise coring + temporal IIR denoiser:
     *    Indoor mid-dark scene (raw=140) with isolated 1023 hot pixels must NOT crush luma,
     *    and dark-cap high-gain noise (64 +- 18, gain=3840) must core cleanly near black. */
    for (uint16_t y = 0; y < sh; y += 2u) {
        for (uint16_t x = 0; x < sw; x += 2u) {
            uint16_t v = 160u;
            if (x == 32u && y == 32u) { v = 1023u; } /* isolated saturated hot quad */
            pack_raw10_quad(raw, stride, x, y, v, v, v, v);
        }
    }
    dgain = 0;
    const uint16_t hot_luma = zizhan_cam_isp_raw10_to_bgra_ex(
        raw, sw, sh, stride, ZIZHAN_BAYER_GRBG, 90, 0, 0,
        64u, 256u, 256u, 1024u, 1, &dgain, out_rear, 120u, 160u,
        NULL, NULL, NULL, NULL);
    assert(hot_luma >= 120u); /* Previously single 1023 hot pixel crushed luma < 85! */

    /* High-gain dark cap (mean 64 +- 14 readout noise at 15x gain=3840): output mean luma <= 6 */
    for (uint16_t y = 0; y < sh; y += 2u) {
        for (uint16_t x = 0; x < sw; x += 2u) {
            const uint16_t n0 = (uint16_t)(54u + (((x * 13u + y * 7u) % 21u)));
            const uint16_t n1 = (uint16_t)(54u + (((x * 17u + y * 11u) % 21u)));
            pack_raw10_quad(raw, stride, x, y, n0, n1, n0, n1);
        }
    }
    uint16_t mr = 0, mg = 0, mb = 0, rq = 0, bq = 0;
    dgain = 0;
    const uint16_t dark_luma = zizhan_cam_isp_raw10_to_bgra_ex(
        raw, sw, sh, stride, ZIZHAN_BAYER_GRBG, 90, 0, 0,
        64u, 256u, 256u, 3840u, 1, &dgain, out_rear, 120u, 160u,
        NULL, &mr, &mg, &mb);
    zizhan_cam_awb_gains(mr, mg, mb, 64u, &rq, &bq);
    assert(rq == 256u && bq == 256u); /* Dark noise must NOT skew AWB away from neutral */
    assert(dark_luma <= 6u);

    /* Temporal denoiser preserves sharp high-contrast motion while smoothing small static noise */
    uint8_t *prev = (uint8_t *)calloc(1, 120u * 160u * 4u);
    assert(prev != NULL);
    int valid = 0;
    memset(out_rear, 100, 120u * 160u * 4u);
    zizhan_cam_temporal_denoise_bgra(out_rear, prev, 120u, 160u, 120u * 4u, 2048u, &valid);
    assert(valid == 1 && prev[0] == 100u);
    out_rear[0] = 112u; /* +12 static noise -> blended to ~103 */
    out_rear[4] = 240u; /* +140 motion edge -> preserved 100% at 240 */
    zizhan_cam_temporal_denoise_bgra(out_rear, prev, 120u, 160u, 120u * 4u, 2048u, &valid);
    assert(out_rear[0] == 103u);
    assert(out_rear[4] == 240u);

    /* Preview layer orientation mapping across folded (wori=1) and unfolded (wori=1..4),
     * plus exact 2x2 matrix composition proof against live SpringBoard saf transforms:
     *   wori=1: saf=[ 1, 0, 0, 1]  wori=2: saf=[-1, 0, 0,-1]
     *   wori=3: saf=[ 0, 1,-1, 0]  wori=4: saf=[ 0,-1, 1, 0]
     * For every unfolded wori in 1..4, saf(R(turns * pi/2) * v_img) must map:
     *   Image TOP (0, -1) -> (-1, 0)_fb (physical camera top)
     *   Image RIGHT (+1, 0) -> (0, -1)_fb (physical phone right / selfie user right) */
    assert(zizhan_cam_preview_quarter_turns_cw(1, 1) == 0);
    assert(zizhan_cam_preview_quarter_turns_cw(0, 4) == 0);
    assert(zizhan_cam_preview_quarter_turns_cw(0, 1) == 3);
    assert(zizhan_cam_preview_quarter_turns_cw(0, 3) == 2);
    assert(zizhan_cam_preview_quarter_turns_cw(0, 2) == 1);

    static const int kSaf[5][4] = {
        { 0,  0,  0,  0},
        { 1,  0,  0,  1}, /* wori=1 */
        {-1,  0,  0, -1}, /* wori=2 */
        { 0,  1, -1,  0}, /* wori=3 */
        { 0, -1,  1,  0}, /* wori=4 */
    };
    static const int kCos[4] = { 1,  0, -1,  0};
    static const int kSin[4] = { 0,  1,  0, -1};
    for (long wori = 1; wori <= 4; ++wori) {
        const int t = zizhan_cam_preview_quarter_turns_cw(0, wori) & 3;
        /* CoreGraphics row-vector rotation: (x', y') = (x*cos - y*sin, x*sin + y*cos) */
        const int top_wx = 0 * kCos[t] - (-1) * kSin[t];
        const int top_wy = 0 * kSin[t] + (-1) * kCos[t];
        const int top_fx = top_wx * kSaf[wori][0] + top_wy * kSaf[wori][2];
        const int top_fy = top_wx * kSaf[wori][1] + top_wy * kSaf[wori][3];
        assert(top_fx == -1 && top_fy == 0);

        const int rt_wx = 1 * kCos[t] - 0 * kSin[t];
        const int rt_wy = 1 * kSin[t] + 0 * kCos[t];
        const int rt_fx = rt_wx * kSaf[wori][0] + rt_wy * kSaf[wori][2];
        const int rt_fy = rt_wx * kSaf[wori][1] + rt_wy * kSaf[wori][3];
        assert(rt_fx == 0 && rt_fy == -1);
    }

    free(prev);
    free(out_rear);
    free(raw);
}

int main(void) {
    assert(sizeof(ZizhanCamFrameHeader) == 64u);
    test_ae_closed_loop();
    test_awb_and_isp_bgra();
    test_cam04_orientation_bayer_hotpixel_and_denoise();
    puts("PASS avcapture18");
    return 0;
}
