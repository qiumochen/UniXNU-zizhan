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

    /* Also test QR stamp bounds + pure-C ISO-18004 QR scanner on noisy optical 640x480 BGRA frame */
    uint8_t *f640 = (uint8_t *)calloc(1, ZIZHAN_CAM_OUT_BYTES);
    assert(f640 != NULL);
    for (size_t i = 0; i < ZIZHAN_CAM_OUT_BYTES; i += 4u) {
        f640[i + 0u] = (uint8_t)(40u + ((i * 17u) & 63u));
        f640[i + 1u] = (uint8_t)(60u + ((i * 31u) & 63u));
        f640[i + 2u] = (uint8_t)(50u + ((i * 13u) & 63u));
        f640[i + 3u] = 255u;
    }
    zizhan_cam_stamp_test_qr(f640, ZIZHAN_CAM_OUT_W, ZIZHAN_CAM_OUT_H);
    /* Bottom-right quiet zone pixel (640-16-1, 480-16-1) must be white (255,255,255,255) */
    const uint8_t *qz = f640 + ((size_t)(480 - 17) * 640 + (640 - 17)) * 4u;
    assert(qz[0] == 255u && qz[1] == 255u && qz[2] == 255u && qz[3] == 255u);

    char qr_msg[128] = {0};
    uint16_t qr_box[4] = {0};
    const int qr_ok = zizhan_qr_scan_bgra(f640, ZIZHAN_CAM_OUT_W, ZIZHAN_CAM_OUT_H,
                                          ZIZHAN_CAM_OUT_STRIDE, qr_msg, sizeof(qr_msg), qr_box);
    assert(qr_ok == 1);
    assert(strcmp(qr_msg, "https://unixnu.org/zizhan-qr-ok") == 0);
    assert(qr_box[0] >= 440u && qr_box[1] >= 280u && qr_box[2] >= 120u && qr_box[3] >= 120u);

    free(f640);
    free(bgra);
    free(raw);
}

int main(void) {
    assert(sizeof(ZizhanCamFrameHeader) == 64u);
    test_ae_closed_loop();
    test_awb_and_isp_bgra();
    puts("PASS avcapture18");
    return 0;
}
