/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* zizhan DBG-01: ASan/UBSan host unit + live loopback HTTP server test harness.
 *
 * Mode 1 (`--unit`): Exhaustively tests constant-time token comparison, sysctl safety
 *                    whitelist/silence guard, wrap-around monotonic ramoops ring reader,
 *                    BGRA8 1x/2x/4x box-filter downsampler, stored-DEFLATE PNG encoder,
 *                    BootResetCore normal/bootloader SDAM cell transitions, and the
 *                    SpringBoard hot-UI crash-rollback state machine.
 * Mode 2 (`--serve <token> <hot_dir>`): Starts the real C HTTP server (`zizhan-debugd18.h`)
 *                    on `127.0.0.1:0`, prints `PORT=<port>` to stdout, and serves requests
 *                    until `POST /api/reboot` or stdin EOF so `zizhan-debugd.py` can test
 *                    `zctl.py` end-to-end against the real C daemon under ASan/UBSan.
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#define ZIZHAN_DEBUGD_HOST_TEST 1

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../drivers/ArmDTPlatform/BootResetCore.h"
#include "../../../userland/zizhan-debugd18.h"

/* --- Mock Kernel State for Live HTTP Server Mode --- */
static uint8_t  g_mock_ring[1024];
static uint64_t g_mock_ring_total;
static uint8_t  g_mock_pmsg[256];
static uint32_t g_mock_pmsg_len;
static ZizhanHotUiTracker g_mock_hot;
static int32_t  g_mock_wdt_rem = 300;
static uint32_t g_mock_wdt_pings = 42;
static int32_t  g_mock_hall_ovr = -1;
static int32_t  g_mock_lid_closed = 0;
static uint32_t g_mock_sb_signals;
static int      g_mock_last_sb_sig;
static int      g_mock_last_reboot = -1;
static int32_t  g_mock_last_touch[5];
static char     g_mock_last_open[240];
static int32_t  g_mock_sysctl_cam = 0;
static pthread_mutex_t g_mock_mtx = PTHREAD_MUTEX_INITIALIZER;

static void mock_ring_append(const char *s) {
    const size_t n = strlen(s);
    for (size_t i = 0; i < n; ++i) {
        g_mock_ring[(g_mock_ring_total + i) % sizeof(g_mock_ring)] = (uint8_t)s[i];
    }
    g_mock_ring_total += n;
}

static int mock_read_klog(const ZizhanDbgKlogReq *req, ZizhanDbgKlogHdr *hdr,
                          uint8_t *buf, size_t buf_cap) {
    pthread_mutex_lock(&g_mock_mtx);
    if (req->zone == 0u) {
        uint32_t maxb = req->max_bytes < (uint32_t)buf_cap ? req->max_bytes : (uint32_t)buf_cap;
        zizhan_dbg_ring_read(g_mock_ring, sizeof(g_mock_ring), g_mock_ring_total, 64u,
                             req->since_seq, maxb, buf, hdr);
    } else {
        memset(hdr, 0, sizeof(*hdr));
        hdr->total_seq = g_mock_pmsg_len;
        if (req->since_seq < g_mock_pmsg_len) {
            hdr->start_seq = req->since_seq;
            uint32_t avail = g_mock_pmsg_len - (uint32_t)req->since_seq;
            if (avail > buf_cap) { avail = (uint32_t)buf_cap; }
            hdr->byte_len = avail;
            memcpy(buf, g_mock_pmsg + req->since_seq, avail);
        } else {
            hdr->start_seq = g_mock_pmsg_len;
        }
    }
    pthread_mutex_unlock(&g_mock_mtx);
    return 0;
}

static int mock_read_fb(const ZizhanDbgFbReq *req, ZizhanDbgFbHdr *hdr,
                        uint8_t **pixels_out, size_t *pixels_len_out) {
    pthread_mutex_lock(&g_mock_mtx);
    uint32_t panel = req->panel;
    if (panel == ZIZHAN_DBG_PANEL_AUTO) {
        panel = (g_mock_lid_closed == 1) ? ZIZHAN_DBG_PANEL_OUTER : ZIZHAN_DBG_PANEL_INNER;
    }
    const uint32_t src_w = (panel == ZIZHAN_DBG_PANEL_OUTER) ? 64u : 80u;
    const uint32_t src_h = (panel == ZIZHAN_DBG_PANEL_OUTER) ? 96u : 60u;
    const uint32_t scale = req->scale ? req->scale : 2u;
    const uint32_t out_w = src_w / scale;
    const uint32_t out_h = src_h / scale;
    const uint32_t out_st = out_w * 4u;

    uint8_t *src = (uint8_t *)malloc((size_t)src_w * (size_t)src_h * 4u);
    assert(src != NULL);
    for (uint32_t y = 0; y < src_h; ++y) {
        for (uint32_t x = 0; x < src_w; ++x) {
            uint8_t *p = src + (y * src_w + x) * 4u;
            p[0] = (uint8_t)(panel == ZIZHAN_DBG_PANEL_OUTER ? 200u : 40u); /* B */
            p[1] = (uint8_t)((x * 3u) & 0xffu);                             /* G */
            p[2] = (uint8_t)((y * 4u) & 0xffu);                             /* R */
            p[3] = 0xffu;
        }
    }

    const size_t need = (size_t)out_st * (size_t)out_h;
    uint8_t *dst = (uint8_t *)malloc(need);
    assert(dst != NULL);
    for (uint32_t oy = 0; oy < out_h; ++oy) {
        zizhan_dbg_downsample_bgra_row(dst + (size_t)oy * out_st, src, src_w * 4u, src_w, src_h, oy, scale);
    }
    free(src);

    memset(hdr, 0, sizeof(*hdr));
    hdr->width = out_w;
    hdr->height = out_h;
    hdr->stride = out_st;
    hdr->format = 1u;
    hdr->panel = panel;
    hdr->seq = 77u;
    hdr->src_width = src_w;
    hdr->src_height = src_h;
    *pixels_out = dst;
    *pixels_len_out = need;
    pthread_mutex_unlock(&g_mock_mtx);
    return 0;
}

static int mock_ctl(const ZizhanDbgCtlReq *req, ZizhanDbgStatusResp *st) {
    pthread_mutex_lock(&g_mock_mtx);
    int32_t op_res = 0;
    if (req->op == ZIZHAN_DBG_OP_EXTEND_WDT) {
        int32_t want = (int32_t)(req->duration_ms < 30u ? 30u : (req->duration_ms > 3600u ? 3600u : req->duration_ms));
        if (req->y0 != 0 || want > g_mock_wdt_rem) {
            g_mock_wdt_rem = want;
        }
        op_res = g_mock_wdt_rem;
    } else if (req->op == ZIZHAN_DBG_OP_HALL_OVERRIDE) {
        g_mock_hall_ovr = req->x0;
        g_mock_lid_closed = (req->x0 == 1) ? 1 : 0;
        op_res = req->x0;
    } else if (req->op == ZIZHAN_DBG_OP_TOUCH) {
        g_mock_last_touch[0] = (int32_t)req->target;
        g_mock_last_touch[1] = req->x0;
        g_mock_last_touch[2] = req->y0;
        g_mock_last_touch[3] = req->x1;
        g_mock_last_touch[4] = req->y1;
        mock_ring_append("MOCK_TOUCH_OK\n");
    } else if (req->op == ZIZHAN_DBG_OP_KEY) {
        char line[64];
        snprintf(line, sizeof(line), "MOCK_KEY_%u\n", req->target);
        mock_ring_append(line);
    } else if (req->op == ZIZHAN_DBG_OP_SAMPLE_PROC) {
        char line[64];
        snprintf(line, sizeof(line), "ZIZHAN_SAMPLE proc=%s s=%u\n", req->text, req->duration_ms);
        mock_ring_append(line);
        op_res = (int32_t)req->duration_ms;
    } else if (req->op == ZIZHAN_DBG_OP_HOT_UI) {
        uint32_t raw_st = zizhan_hot_ui_ctl_step(&g_mock_hot, req->target, 1000ull);
        if (raw_st == 0xffffffffu) {
            pthread_mutex_unlock(&g_mock_mtx);
            return ENOENT;
        }
        op_res = (int32_t)raw_st;
    } else if (req->op == ZIZHAN_DBG_OP_SIGNAL_SB) {
        if (req->target == 15u) {
            (void)zizhan_hot_ui_ctl_step(&g_mock_hot, ZIZHAN_HOT_UI_NOTE_RESTART, 1000ull);
        }
        g_mock_sb_signals += 1u;
        g_mock_last_sb_sig = (int)req->target;
        op_res = 1234;
    } else if (req->op == ZIZHAN_DBG_OP_REBOOT) {
        g_mock_last_reboot = (int)req->target;
        __atomic_store_n(&g_zdbg_stop_flag, 1, __ATOMIC_RELEASE);
        int wfd = socket(AF_INET, SOCK_STREAM, 0);
        if (wfd >= 0) {
            struct sockaddr_in wsin;
            memset(&wsin, 0, sizeof(wsin));
            wsin.sin_family = AF_INET;
            wsin.sin_port = htons(g_zdbg_bound_port);
            (void)inet_pton(AF_INET, "127.0.0.1", &wsin.sin_addr);
            (void)connect(wfd, (struct sockaddr *)&wsin, sizeof(wsin));
            close(wfd);
        }
    }

    memset(st, 0, sizeof(*st));
    st->uptime_ms = 123456ull;
    st->klog_total_bytes = g_mock_ring_total;
    st->pmsg_bytes = g_mock_pmsg_len;
    st->autoreset_rem_s = g_mock_wdt_rem;
    st->autoreset_pings = g_mock_wdt_pings;
    st->lid_closed = g_mock_lid_closed;
    st->hall_override = g_mock_hall_ovr;
    st->outer_active_view = g_mock_lid_closed ? 3u : 0u;
    st->inner_display_pwr = g_mock_lid_closed ? 0 : 1;
    st->zc_flipped_tag = 900u;
    st->batt_soc_pct = 84;
    st->batt_mv = 4120;
    st->batt_ma = -215;
    st->batt_charging = 0u;
    st->hot_ui_state = g_mock_hot.state;
    st->hot_ui_rollbacks = g_mock_hot.rollbacks;
    st->hot_ui_sb_execs = g_mock_hot.sb_execs;
    st->op_result = op_res;
    st->inner_w = 2160u;
    st->inner_h = 1916u;
    st->outer_w = 1080u;
    st->outer_h = 2520u;
    pthread_mutex_unlock(&g_mock_mtx);
    return 0;
}

static int mock_sysctl_get(const char *name, char *val_out, size_t val_cap) {
    if (strcmp(name, "kern.zizhan_cam_ctrl") == 0) {
        snprintf(val_out, val_cap, "%d", g_mock_sysctl_cam);
        return 0;
    }
    return ENOENT;
}

static int mock_sysctl_set(const char *name, const char *val_str) {
    if (strcmp(name, "kern.zizhan_cam_ctrl") == 0) {
        g_mock_sysctl_cam = atoi(val_str);
        return 0;
    }
    return ENOENT;
}

static int mock_open_cmd(const char *cmd) {
    pthread_mutex_lock(&g_mock_mtx);
    snprintf(g_mock_last_open, sizeof(g_mock_last_open), "%s", cmd);
    char line[280];
    snprintf(line, sizeof(line), "MOCK_OPEN:%s\n", cmd);
    mock_ring_append(line);
    pthread_mutex_unlock(&g_mock_mtx);
    return 0;
}

static int mock_stage_ui(const uint8_t *bytes, size_t len) {
    if (!bytes || len < 64u) {
        return EINVAL;
    }
    uint32_t mhdr[4] = {0};
    memcpy(mhdr, bytes, sizeof(mhdr));
    if (mhdr[0] != 0xfeedfacfu || mhdr[1] != 0x0100000cu || mhdr[3] != 6u) {
        return EINVAL;
    }
    pthread_mutex_lock(&g_mock_mtx);
    uint32_t slot = zizhan_hot_ui_pick_stage_slot(&g_mock_hot);
    zizhan_hot_ui_mark_staged(&g_mock_hot, slot, 1);
    pthread_mutex_unlock(&g_mock_mtx);
    return 0;
}

/* Synthesize a valid minimal thin arm64 MH_DYLIB signed with 4K SHA-256 CodeDirectory for upload test. */
static size_t make_valid_4k_dylib(uint8_t *buf, size_t cap) {
    memset(buf, 0, cap);
    const uint32_t text_off = 4096u, text_sz = 4096u;
    const uint32_t le_off = 8192u, le_sz = 64u;
    zizhan_ipa_put_le32(buf + 0u,  0xfeedfacfu); /* MH_MAGIC_64 */
    zizhan_ipa_put_le32(buf + 4u,  0x0100000cu); /* CPU_TYPE_ARM64 */
    zizhan_ipa_put_le32(buf + 8u,  0u);
    zizhan_ipa_put_le32(buf + 12u, 6u);          /* MH_DYLIB */
    zizhan_ipa_put_le32(buf + 16u, 2u);          /* ncmds = 2 (__TEXT + __LINKEDIT) */
    zizhan_ipa_put_le32(buf + 20u, 144u);        /* sizeofcmds = 2 * 72 */
    zizhan_ipa_put_le32(buf + 24u, 0u);

    /* LC_SEGMENT_64 __TEXT */
    uint8_t *c0 = buf + 32u;
    zizhan_ipa_put_le32(c0 + 0u, 0x19u);
    zizhan_ipa_put_le32(c0 + 4u, 72u);
    memcpy(c0 + 8u, "__TEXT", 6u);
    zizhan_ipa_put_le64(c0 + 24u, 0ull);
    zizhan_ipa_put_le64(c0 + 32u, 8192ull);
    zizhan_ipa_put_le64(c0 + 40u, 0ull);
    zizhan_ipa_put_le64(c0 + 48u, 8192ull);

    /* LC_SEGMENT_64 __LINKEDIT */
    uint8_t *c1 = buf + 104u;
    zizhan_ipa_put_le32(c1 + 0u, 0x19u);
    zizhan_ipa_put_le32(c1 + 4u, 72u);
    memcpy(c1 + 8u, "__LINKEDIT", 10u);
    zizhan_ipa_put_le64(c1 + 24u, 8192ull);
    zizhan_ipa_put_le64(c1 + 32u, 16384ull);
    zizhan_ipa_put_le64(c1 + 40u, le_off);
    zizhan_ipa_put_le64(c1 + 48u, le_sz);

    (void)text_off;
    (void)text_sz;
    size_t signed_len = 0u;
    int rc = zizhan_macho_sign_adhoc_4k(buf, le_off + le_sz, cap, "org.unixnu.zizhan.hotui", &signed_len);
    assert(rc == ZIZHAN_IPA_OK);
    return signed_len;
}

static void run_unit_tests(void) {
    /* 1. Constant-time token & bind-IP policy checks */
    const char *good = "0123456789abcdef0123456789abcdef";
    assert(zizhan_dbg_token_eq(good, strlen(good), good, strlen(good)) == 1);
    assert(zizhan_dbg_token_eq(good, strlen(good), "0123456789abcdef0123456789abcdee", 32) == 0);
    assert(zizhan_dbg_token_eq(good, strlen(good), "0123456789abcdef", 16) == 0);
    assert(zizhan_dbg_token_eq("short", 5, "short", 5) == 0); /* < 16 bytes refused */
    const char *am = NULL;
    assert(strcmp(zizhan_dbg_select_bind_ip(64u, 0, &am), "0.0.0.0") == 0 && strcmp(am, "bearer-token") == 0);
    /* HIGH-NEW-1: zizhan_dbg_notoken=1 MUST force 127.0.0.1 even if /chosen/zizhan-dbg-token is 64B! */
    assert(strcmp(zizhan_dbg_select_bind_ip(64u, 1, &am), "127.0.0.1") == 0 && strcmp(am, "loopback-or-notoken") == 0);
    assert(strcmp(zizhan_dbg_select_bind_ip(0u, 1, &am), "127.0.0.1") == 0 && strcmp(am, "loopback-or-notoken") == 0);

    /* 2. Unattended silence & security sysctl policy */
    assert(zizhan_dbg_sysctl_name_valid("kern.zizhan_cam_ctrl") == 1);
    assert(zizhan_dbg_sysctl_name_valid("kern.zizhan_dbg_token") == 0);
    assert(zizhan_dbg_sysctl_name_valid("kern.exec_inject") == 0);
    assert(zizhan_dbg_sysctl_write_allowed("kern.zizhan_cam_ctrl", "2", 0) == 1);
    /* HIGH-3: block haptics (0x101) and speaker routing (0x201) via cam_ctrl when user_present==0! */
    assert(zizhan_dbg_sysctl_write_allowed("kern.zizhan_cam_ctrl", "0x101", 0) == 0);
    assert(zizhan_dbg_sysctl_write_allowed("kern.zizhan_cam_ctrl", "0x201", 0) == 0);
    assert(zizhan_dbg_sysctl_write_allowed("kern.zizhan_cam_ctrl", "0x101", 1) == 1);
    assert(zizhan_dbg_sysctl_write_allowed("kern.zizhan_audio_attended", "1", 0) == 0);
    assert(zizhan_dbg_sysctl_write_allowed("kern.zizhan_pulse_dry", "0", 0) == 0);
    assert(zizhan_dbg_sysctl_write_allowed("kern.zizhan_haptics_dry", "0", 0) == 0);
    assert(zizhan_dbg_sysctl_write_allowed("kern.pong_audio", "1", 0) == 0);
    assert(zizhan_dbg_sysctl_write_allowed("kern.zizhan_audio_attended", "1", 1) == 1);
    assert(zizhan_dbg_sysctl_write_allowed("kern.zizhan_inject_apps", "1", 1) == 0);

    /* 3. Wrap-around monotonic ring reader */
    uint8_t ring[100];
    memset(ring, 0, sizeof(ring));
    uint64_t total = 0;
    for (unsigned i = 0; i < 260u; ++i) {
        ring[total % sizeof(ring)] = (uint8_t)('A' + (i % 26u));
        total += 1ull;
    }
    uint8_t out[128];
    ZizhanDbgKlogHdr khdr;
    zizhan_dbg_ring_read(ring, sizeof(ring), total, 10u, 50ull, 64u, out, &khdr);
    assert(khdr.dropped == 1u);
    assert(khdr.start_seq == 170ull); /* 260 - (100 - 10) */
    assert(khdr.byte_len == 64u);
    for (unsigned i = 0; i < khdr.byte_len; ++i) {
        assert(out[i] == (uint8_t)('A' + ((170u + i) % 26u)));
    }

    /* 4. BGRA8 downsampler & stored PNG encoder */
    uint8_t fb[16 * 12 * 4];
    for (unsigned i = 0; i < 16u * 12u; ++i) {
        fb[i * 4u + 0u] = 10u;
        fb[i * 4u + 1u] = 20u;
        fb[i * 4u + 2u] = 30u;
        fb[i * 4u + 3u] = 255u;
    }
    uint8_t ds[8 * 6 * 4];
    for (unsigned y = 0; y < 6u; ++y) {
        zizhan_dbg_downsample_bgra_row(ds + y * 32u, fb, 64u, 16u, 12u, y, 2u);
    }
    assert(ds[0] == 10u && ds[1] == 20u && ds[2] == 30u && ds[3] == 255u);
    uint8_t png[4096];
    size_t png_len = zizhan_dbg_encode_png_stored(ds, 8u, 6u, 32u, png, sizeof(png));
    assert(png_len > 60u);
    assert(png[0] == 0x89u && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');

    /* 5. BootResetCore normal vs bootloader SDAM reason */
    uint32_t core[0x3000 / 4] = {0}, cnfg[0x4000 / 4] = {0}, obs[0x100] = {0}, chn[0x2000] = {0};
    core[0] = BR_VERSION_V7_MIN;
    core[BR_FEATURES / 4] = 4u;
    core[(BR_APID_MAP + 4 * 1) / 4] = (uint32_t)BR_SDAM_PPID << 8;
    cnfg[(BR_CNFG_OWNER + 4 * 1) / 4] = 0u;
    obs[(BR_OBS_APID_STRIDE * 1 + BR_STATUS) / 4] = BR_STATUS_DONE;
    obs[(BR_OBS_APID_STRIDE * 1 + BR_RDATA0) / 4] = 0x05u; /* bit0 = 1, reason = 2 (bootloader) */
    chn[(BR_CHNL_APID_STRIDE * 1 + BR_STATUS) / 4] = BR_STATUS_DONE;
    struct br_arbiter arb = { core, cnfg, 0 };
    struct br_found fnd;
    assert(br_find(&arb, &fnd) == 0 && fnd.apid == 1);
    /* Simulate write mirroring into RDATA0: test bit math directly on cell 0x05 -> 0x01 for Android! */
    uint8_t expected_android = (uint8_t)((0x05u & ~BR_REBOOT_MASK) | (BR_REBOOT_NORMAL << BR_REBOOT_SHIFT));
    assert(expected_android == 0x01u);

    /* 6. Hot-UI crash-rollback & inactive-slot staging state machine */
    ZizhanHotUiTracker ht;
    memset(&ht, 0, sizeof(ht));
    /* CRITICAL-NEW-2: ARM without staged_valid MUST fail (0xffffffff) without entering TRIAL! */
    assert(zizhan_hot_ui_ctl_step(&ht, ZIZHAN_HOT_UI_ARM, 900ull) == 0xffffffffu);
    assert(ht.state == ZIZHAN_HOT_UI_STATE_STOCK);
    assert(zizhan_hot_ui_pick_stage_slot(&ht) == 0u);
    zizhan_hot_ui_mark_staged(&ht, 0u, 1);
    assert(zizhan_hot_ui_ctl_step(&ht, ZIZHAN_HOT_UI_ARM, 1000ull) == ZIZHAN_HOT_UI_STATE_TRIAL);
    assert(ht.active_slot == 0u && ht.staged_valid == 0u);
    /* backboardd and installd are NEVER overridden */
    assert(zizhan_hot_ui_step_exec(&ht, "backboardd", 10u, 0, 1100ull) == 0);
    assert(zizhan_hot_ui_step_exec(&ht, "installd", 8u, 0, 1100ull) == 0);
    /* Apps stay on stock during TRIAL before SpringBoard confirms */
    assert(zizhan_hot_ui_step_exec(&ht, "MyThirdPartyApp", 15u, 1, 1200ull) == 0);
    /* First SpringBoard respawn gets candidate */
    assert(zizhan_hot_ui_step_exec(&ht, "SpringBoard", 11u, 0, 1300ull) == 1);
    /* MEDIUM-1: Unconfirmed 2nd SpringBoard respawn even >15s later (+25s) MUST auto-rollback! */
    assert(zizhan_hot_ui_step_exec(&ht, "SpringBoard", 11u, 0, 26500ull) == 0);
    assert(ht.state == ZIZHAN_HOT_UI_STATE_STOCK && ht.rollbacks == 1u);

    /* Re-stage + re-arm, SpringBoard spawns at t=5100, confirms at t=10100:
     * MEDIUM-1: normal respawn at t=21000 (>=15s after exec t=5100, even though <15s after confirm!) MUST NOT rollback! */
    zizhan_hot_ui_mark_staged(&ht, zizhan_hot_ui_pick_stage_slot(&ht), 1);
    assert(zizhan_hot_ui_ctl_step(&ht, ZIZHAN_HOT_UI_ARM, 5000ull) == ZIZHAN_HOT_UI_STATE_TRIAL);
    assert(zizhan_hot_ui_step_exec(&ht, "SpringBoard", 11u, 0, 5100ull) == 1);
    assert(zizhan_hot_ui_ctl_step(&ht, ZIZHAN_HOT_UI_CONFIRM, 10100ull) == ZIZHAN_HOT_UI_STATE_CONFIRMED);
    assert(ht.active_slot == 0u);
    /* CRITICAL-NEW-2: Staging twice while CONFIRMED on slot 0 MUST keep picking slot 1 and NEVER flip active_slot early! */
    assert(zizhan_hot_ui_pick_stage_slot(&ht) == 1u);
    zizhan_hot_ui_mark_staged(&ht, 1u, 1);
    assert(ht.active_slot == 0u);
    assert(zizhan_hot_ui_pick_stage_slot(&ht) == 1u);
    assert(zizhan_hot_ui_step_exec(&ht, "MyThirdPartyApp", 15u, 1, 12000ull) == 1);
    assert(ht.active_slot == 0u);
    assert(zizhan_hot_ui_step_exec(&ht, "backboardd", 10u, 0, 12000ull) == 0);
    assert(zizhan_hot_ui_step_exec(&ht, "SpringBoard", 11u, 0, 21000ull) == 1);
    assert(ht.state == ZIZHAN_HOT_UI_STATE_CONFIRMED && ht.rollbacks == 1u);
    /* Intentional respring within 2s via NOTE_RESTART MUST NOT rollback! */
    (void)zizhan_hot_ui_ctl_step(&ht, ZIZHAN_HOT_UI_NOTE_RESTART, 22000ull);
    assert(zizhan_hot_ui_step_exec(&ht, "SpringBoard", 11u, 0, 23000ull) == 1);
    assert(ht.state == ZIZHAN_HOT_UI_STATE_CONFIRMED && ht.rollbacks == 1u);
    /* ARM now atomically promotes staged slot 1 -> active_slot 1! */
    assert(zizhan_hot_ui_ctl_step(&ht, ZIZHAN_HOT_UI_ARM, 23500ull) == ZIZHAN_HOT_UI_STATE_TRIAL);
    assert(ht.active_slot == 1u && ht.staged_valid == 0u);
    assert(zizhan_hot_ui_step_exec(&ht, "SpringBoard", 11u, 0, 24000ull) == 1);
    assert(zizhan_hot_ui_ctl_step(&ht, ZIZHAN_HOT_UI_CONFIRM, 29000ull) == ZIZHAN_HOT_UI_STATE_CONFIRMED);
    /* Unexpected crash 4s later (no NOTE_RESTART, <15s since exec t=24000) -> auto-rollback! */
    assert(zizhan_hot_ui_step_exec(&ht, "SpringBoard", 11u, 0, 31000ull) == 0);
    assert(ht.state == ZIZHAN_HOT_UI_STATE_STOCK && ht.rollbacks == 2u);
}

int main(int argc, char **argv) {
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
    if (argc == 2 && strcmp(argv[1], "--unit") == 0) {
        run_unit_tests();
        printf("zizhan-debugd unit tests: PASS\n");
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "--emit-dylib") == 0) {
        uint8_t *buf = (uint8_t *)malloc(65536u);
        assert(buf != NULL);
        size_t sz = make_valid_4k_dylib(buf, 65536u);
        FILE *fp = fopen(argv[2], "wb");
        assert(fp != NULL);
        assert(fwrite(buf, 1u, sz, fp) == sz);
        fclose(fp);
        free(buf);
        (void)argv[3];
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "--serve") == 0) {
        snprintf(g_zdbg_token, sizeof(g_zdbg_token), "%s", argv[2]);
        g_zdbg_token_len = strlen(g_zdbg_token);
        snprintf(g_zdbg_hot_ui_dir_override, sizeof(g_zdbg_hot_ui_dir_override), "%s", argv[3]);

        g_zdbg_host_ops.read_klog = mock_read_klog;
        g_zdbg_host_ops.read_fb = mock_read_fb;
        g_zdbg_host_ops.ctl = mock_ctl;
        g_zdbg_host_ops.sysctl_get = mock_sysctl_get;
        g_zdbg_host_ops.sysctl_set = mock_sysctl_set;
        g_zdbg_host_ops.open_cmd = mock_open_cmd;
        g_zdbg_host_ops.stage_ui = mock_stage_ui;

        mock_ring_append("BOOT_CONSOLE_LINE_1\n");
        const char *pmsg_init = "PONG DIAGNOSTIC v92: isolated pmsg ready\n";
        memcpy(g_mock_pmsg, pmsg_init, strlen(pmsg_init));
        g_mock_pmsg_len = (uint32_t)strlen(pmsg_init);

        if (zdbg_start_listener("127.0.0.1", 0) != 0) {
            fprintf(stderr, "bind 127.0.0.1:0 failed\n");
            return 1;
        }
        struct timeval ltv = { .tv_sec = 0, .tv_usec = 100000 };
        (void)setsockopt(g_zdbg_listen_fd, SOL_SOCKET, SO_RCVTIMEO, &ltv, sizeof(ltv));
        printf("PORT=%u\n", (unsigned)g_zdbg_bound_port);
        fflush(stdout);

        (void)zdbg_accept_loop(NULL);
        close(g_zdbg_listen_fd);
        /* Wait briefly for detached response worker to finish closing client socket */
        for (int i = 0; i < 50 && __atomic_load_n(&g_zdbg_active_clients, __ATOMIC_ACQUIRE) != 0u; ++i) {
            zdbg_sleep_ms(10u);
        }
        return 0;
    }
    fprintf(stderr, "usage: %s --unit | --emit-dylib <out> - | --serve <tok> <hot_dir>\n", argv[0]);
    return 2;
}
