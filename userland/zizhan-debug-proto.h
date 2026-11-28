/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* zizhan DBG-01: Shared LAN live-debug protocol, ring reader, BGRA8 downsampler,
 * stored-DEFLATE PNG fallback, sysctl write guard, and hot-UI crash-rollback machine.
 * Pure C99 / C++17 header with zero OS dependencies so kernel, iPadOS userland, and
 * Linux/macOS ASan/UBSan host tests compile the exact same logic.
 */
#ifndef ZIZHAN_DEBUG_PROTO_H
#define ZIZHAN_DEBUG_PROTO_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZIZHAN_DBG_DEFAULT_PORT 8089u
#define ZIZHAN_DBG_TOKEN_MIN_LEN 16u
#define ZIZHAN_DBG_TOKEN_MAX_LEN 256u
#define ZIZHAN_DBG_KLOG_DEFAULT_MAX 65536u
#define ZIZHAN_DBG_KLOG_HARD_MAX 262144u
#define ZIZHAN_HOT_UI_SLOT0_PATH "/private/var/db/zizhan-hot-ui-0.dylib"
#define ZIZHAN_HOT_UI_SLOT1_PATH "/private/var/db/zizhan-hot-ui-1.dylib"
#define ZIZHAN_STOCK_UI_PATH "/pong/libpong-ui18.dylib"
#define ZIZHAN_DBG_DARWIN_NOTIFY "org.unixnu.zizhan.debug.cmd"
#define ZIZHAN_DBG_CMD_DIR "/private/var/mobile/Library/Pong"
#define ZIZHAN_DBG_CMD_NAME "debug-open-cmd.txt"
#define ZIZHAN_DBG_CMD_FILE "/private/var/mobile/Library/Pong/debug-open-cmd.txt"

enum ZizhanDbgOp {
    ZIZHAN_DBG_OP_STATUS        = 0,
    ZIZHAN_DBG_OP_EXTEND_WDT    = 1,
    ZIZHAN_DBG_OP_REBOOT        = 2,
    ZIZHAN_DBG_OP_TOUCH         = 3,
    ZIZHAN_DBG_OP_KEY           = 4,
    ZIZHAN_DBG_OP_HALL_OVERRIDE = 5,
    ZIZHAN_DBG_OP_SAMPLE_PROC   = 6,
    ZIZHAN_DBG_OP_HOT_UI        = 7,
    ZIZHAN_DBG_OP_SIGNAL_SB     = 8
};

enum ZizhanDbgPanel {
    ZIZHAN_DBG_PANEL_AUTO  = 0,
    ZIZHAN_DBG_PANEL_INNER = 1,
    ZIZHAN_DBG_PANEL_OUTER = 2,
    ZIZHAN_DBG_PANEL_NORM  = 3
};

enum ZizhanDbgKey {
    ZIZHAN_DBG_KEY_POWER    = 0,
    ZIZHAN_DBG_KEY_VOL_UP   = 1,
    ZIZHAN_DBG_KEY_VOL_DOWN = 2,
    ZIZHAN_DBG_KEY_HOME     = 3
};

enum ZizhanDbgRebootTarget {
    ZIZHAN_DBG_REBOOT_FASTBOOT = 0,
    ZIZHAN_DBG_REBOOT_ANDROID  = 1
};

enum ZizhanHotUiCmd {
    ZIZHAN_HOT_UI_REVERT       = 0,
    ZIZHAN_HOT_UI_CONFIRM      = 1,
    ZIZHAN_HOT_UI_ARM          = 2,
    ZIZHAN_HOT_UI_QUERY        = 3,
    ZIZHAN_HOT_UI_NOTE_RESTART = 4
};

enum ZizhanHotUiMode {
    ZIZHAN_HOT_UI_STATE_STOCK     = 0,
    ZIZHAN_HOT_UI_STATE_CONFIRMED = 1,
    ZIZHAN_HOT_UI_STATE_TRIAL     = 2
};

typedef struct {
    uint64_t since_seq;
    uint32_t zone;       /* 0 = console ring, 1 = diagnostic pmsg */
    uint32_t max_bytes;  /* 0 -> 65536, capped at 262144 */
} ZizhanDbgKlogReq;

typedef struct {
    uint64_t total_seq;  /* monotonic end byte sequence */
    uint64_t start_seq;  /* sequence of first returned byte */
    uint32_t byte_len;   /* payload bytes following header */
    uint32_t dropped;    /* 1 if since_seq fell behind ring window */
} ZizhanDbgKlogHdr;

typedef struct {
    uint32_t panel;      /* ZizhanDbgPanel (0=auto, 1=inner, 2=outer) */
    uint32_t scale;      /* 1, 2, or 4 (0 -> 2) */
    uint32_t flags;      /* 0 */
    uint32_t reserved;
} ZizhanDbgFbReq;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;     /* 1 = BGRA8_SRGB */
    uint32_t panel;      /* actual panel: 1=inner, 2=outer */
    uint32_t seq;
    uint32_t src_width;
    uint32_t src_height;
} ZizhanDbgFbHdr;

typedef struct {
    uint32_t op;          /* ZizhanDbgOp */
    uint32_t target;      /* panel / key / reboot target / hot-ui cmd */
    int32_t  x0;          /* tap/swipe start X or hall override (-1,0,1) */
    int32_t  y0;          /* tap/swipe start Y or wdt exact flag */
    int32_t  x1;          /* swipe end X */
    int32_t  y1;          /* swipe end Y */
    uint32_t duration_ms; /* hold/swipe ms, wdt lease s, or sample s */
    uint32_t steps;       /* 0 = tap (if x1==x0 && y1==y0 && steps==0) or auto swipe steps */
    char     text[32];    /* proc name for SAMPLE_PROC */
} ZizhanDbgCtlReq;

typedef struct {
    uint64_t uptime_ms;
    uint64_t klog_total_bytes;
    uint32_t pmsg_bytes;
    int32_t  autoreset_rem_s;
    uint32_t autoreset_pings;
    int32_t  lid_closed;
    int32_t  hall_override;
    uint32_t outer_active_view;
    int32_t  inner_display_pwr;
    uint32_t zc_flipped_tag;
    int32_t  batt_soc_pct;
    int32_t  batt_mv;
    int32_t  batt_ma;
    uint32_t batt_charging;
    uint32_t cpu_thermal_lvl;
    uint32_t gpu_thermal_lvl;
    uint32_t hot_ui_state;
    uint32_t hot_ui_rollbacks;
    uint32_t hot_ui_sb_execs;
    int32_t  op_result;
    uint32_t inner_w;
    uint32_t inner_h;
    uint32_t outer_w;
    uint32_t outer_h;
    uint32_t reserved[4];
} ZizhanDbgStatusResp;

#ifdef __cplusplus
static_assert(sizeof(ZizhanDbgKlogReq) == 16, "ZizhanDbgKlogReq ABI");
static_assert(sizeof(ZizhanDbgKlogHdr) == 24, "ZizhanDbgKlogHdr ABI");
static_assert(sizeof(ZizhanDbgFbReq) == 16, "ZizhanDbgFbReq ABI");
static_assert(sizeof(ZizhanDbgFbHdr) == 32, "ZizhanDbgFbHdr ABI");
static_assert(sizeof(ZizhanDbgCtlReq) == 64, "ZizhanDbgCtlReq ABI");
static_assert(sizeof(ZizhanDbgStatusResp) == 120, "ZizhanDbgStatusResp ABI");
#else
_Static_assert(sizeof(ZizhanDbgKlogReq) == 16, "ZizhanDbgKlogReq ABI");
_Static_assert(sizeof(ZizhanDbgKlogHdr) == 24, "ZizhanDbgKlogHdr ABI");
_Static_assert(sizeof(ZizhanDbgFbReq) == 16, "ZizhanDbgFbReq ABI");
_Static_assert(sizeof(ZizhanDbgFbHdr) == 32, "ZizhanDbgFbHdr ABI");
_Static_assert(sizeof(ZizhanDbgCtlReq) == 64, "ZizhanDbgCtlReq ABI");
_Static_assert(sizeof(ZizhanDbgStatusResp) == 120, "ZizhanDbgStatusResp ABI");
#endif

/* Constant-time token check: rejects empty/short/oversize tokens and length mismatches. */
static inline int zizhan_dbg_token_eq(const char *expected, size_t exp_len,
                                      const char *provided, size_t prov_len) {
    if (!expected || !provided) {
        return 0;
    }
    if (exp_len < ZIZHAN_DBG_TOKEN_MIN_LEN || exp_len > ZIZHAN_DBG_TOKEN_MAX_LEN) {
        return 0;
    }
    uint32_t diff = (uint32_t)(exp_len ^ prov_len);
    for (size_t i = 0; i < exp_len; ++i) {
        const uint8_t pb = (i < prov_len) ? (uint8_t)provided[i] : 0u;
        diff |= (uint32_t)((uint8_t)expected[i] ^ pb);
    }
    return diff == 0u ? 1 : 0;
}

/* Bind IP policy: zizhan_dbg_notoken=1 MUST strictly bind 127.0.0.1 even if /chosen/zizhan-dbg-token exists. */
static inline const char *zizhan_dbg_select_bind_ip(size_t token_len, int allow_notoken,
                                                    const char **auth_mode_out) {
    const int lan_token_mode = (!allow_notoken &&
                                token_len >= ZIZHAN_DBG_TOKEN_MIN_LEN &&
                                token_len <= ZIZHAN_DBG_TOKEN_MAX_LEN) ? 1 : 0;
    if (auth_mode_out) {
        *auth_mode_out = lan_token_mode ? "bearer-token" : "loopback-or-notoken";
    }
    return lan_token_mode ? "0.0.0.0" : "127.0.0.1";
}

static inline int zizhan_dbg_has_prefix(const char *s, const char *pfx) {
    size_t i = 0;
    while (pfx[i] != '\0') {
        if (s[i] != pfx[i]) {
            return 0;
        }
        ++i;
    }
    return 1;
}

static inline int zizhan_dbg_sysctl_name_valid(const char *name) {
    if (!name || name[0] == '\0') {
        return 0;
    }
    size_t i = 0;
    for (; name[i] != '\0'; ++i) {
        if (i >= 96u) {
            return 0;
        }
        const char c = name[i];
        const int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!ok) {
            return 0;
        }
    }
    if (zizhan_dbg_has_prefix(name, "kern.zizhan_dbg_")) {
        return 0; /* raw debug internal OIDs are never proxied through /api/sysctl */
    }
    if (zizhan_dbg_has_prefix(name, "kern.zizhan_") ||
        zizhan_dbg_has_prefix(name, "kern.pong_") ||
        strcmp(name, "kern.boottime") == 0 ||
        strcmp(name, "kern.bootargs") == 0 ||
        strcmp(name, "kern.osversion") == 0 ||
        strcmp(name, "kern.ostype") == 0 ||
        strcmp(name, "kern.osrelease") == 0 ||
        zizhan_dbg_has_prefix(name, "hw.") ||
        zizhan_dbg_has_prefix(name, "vm.")) {
        return 1;
    }
    return 0;
}

/* Closed-by-default write guard:
 * - Never allows dylib-injection or bootarg OIDs regardless of user presence.
 * - When `user_present == 0` (unattended), ONLY explicitly whitelisted non-audio /
 *   non-haptics diagnostic OIDs may be written, and `kern.zizhan_cam_ctrl` rejects
 *   values outside `[-1 .. 63]` (blocking `0x101..0x107` LRA haptics & `0x200..0x201`
 *   speaker/earpiece audio routing!).
 */
static inline int zizhan_dbg_parse_i64(const char *s, int64_t *out) {
    if (!s || s[0] == '\0' || !out) {
        return 0;
    }
    size_t i = 0u;
    int neg = 0;
    if (s[i] == '-') {
        neg = 1;
        ++i;
    } else if (s[i] == '+') {
        ++i;
    }
    uint32_t base = 10u;
    if (s[i] == '0' && (s[i + 1u] == 'x' || s[i + 1u] == 'X')) {
        base = 16u;
        i += 2u;
    }
    if (s[i] == '\0') {
        return 0;
    }
    uint64_t acc = 0ull;
    const uint64_t lim = neg ? 0x8000000000000000ull : 0x7fffffffffffffffull;
    for (; s[i] != '\0'; ++i) {
        const char c = s[i];
        uint32_t d;
        if (c >= '0' && c <= '9') {
            d = (uint32_t)(c - '0');
        } else if (base == 16u && c >= 'a' && c <= 'f') {
            d = 10u + (uint32_t)(c - 'a');
        } else if (base == 16u && c >= 'A' && c <= 'F') {
            d = 10u + (uint32_t)(c - 'A');
        } else {
            return 0;
        }
        if (acc > (lim - (uint64_t)d) / (uint64_t)base) {
            return 0;
        }
        acc = acc * (uint64_t)base + (uint64_t)d;
    }
    if (neg) {
        *out = (acc == 0x8000000000000000ull) ? (-9223372036854775807ll - 1ll) : -(int64_t)acc;
    } else {
        *out = (int64_t)acc;
    }
    return 1;
}

static inline int zizhan_dbg_sysctl_write_allowed(const char *name, const char *val_str, int user_present) {
    if (!zizhan_dbg_sysctl_name_valid(name) || !val_str || val_str[0] == '\0') {
        return 0;
    }
    if (strcmp(name, "kern.zizhan_inject_apps") == 0 ||
        strcmp(name, "kern.zizhan_bootargs") == 0 ||
        strcmp(name, "kern.bootargs") == 0) {
        return 0;
    }
    if (!user_present) {
        if (strcmp(name, "kern.zizhan_cam_ctrl") == 0) {
            int64_t v = 0;
            if (!zizhan_dbg_parse_i64(val_str, &v)) {
                return 0;
            }
            return (v >= -1 && v <= 63) ? 1 : 0;
        }
        if (strcmp(name, "kern.zizhan_als_autobright") == 0 ||
            strcmp(name, "kern.zizhan_imu_rot") == 0 ||
            strcmp(name, "kern.zizhan_scene_diag") == 0 ||
            strcmp(name, "kern.zizhan_app_diag") == 0 ||
            strcmp(name, "kern.zizhan_net_diag") == 0 ||
            strcmp(name, "kern.zizhan_standby_bw") == 0 ||
            strcmp(name, "kern.zizhan_wifi_ps") == 0) {
            return 1;
        }
        return 0;
    }
    if (zizhan_dbg_has_prefix(name, "kern.zizhan_") ||
        zizhan_dbg_has_prefix(name, "kern.pong_")) {
        return 1;
    }
    return 0;
}

/* Pure-C monotonic circular ring reader (byte `seq` lives at `ring[seq % capacity]`). */
static inline void zizhan_dbg_ring_read(const uint8_t *ring, uint32_t capacity,
                                        uint64_t total_seq, uint32_t head_guard,
                                        uint64_t since_seq, uint32_t max_bytes,
                                        uint8_t *dst, ZizhanDbgKlogHdr *hdr_out) {
    if (!hdr_out) {
        return;
    }
    memset(hdr_out, 0, sizeof(*hdr_out));
    hdr_out->total_seq = total_seq;
    if (!ring || capacity == 0u || total_seq == 0ull) {
        hdr_out->start_seq = total_seq;
        return;
    }
    const uint32_t usable = (capacity > head_guard) ? (capacity - head_guard) : capacity;
    const uint64_t oldest = (total_seq > (uint64_t)usable) ? (total_seq - (uint64_t)usable) : 0ull;
    uint64_t start = since_seq;
    if (start < oldest) {
        start = oldest;
        if (since_seq != 0ull || total_seq > (uint64_t)usable) {
            hdr_out->dropped = 1u;
        }
    }
    if (start >= total_seq) {
        hdr_out->start_seq = total_seq;
        return;
    }
    uint32_t cap = max_bytes ? max_bytes : ZIZHAN_DBG_KLOG_DEFAULT_MAX;
    if (cap > ZIZHAN_DBG_KLOG_HARD_MAX) {
        cap = ZIZHAN_DBG_KLOG_HARD_MAX;
    }
    uint64_t avail = total_seq - start;
    uint32_t count = (avail > (uint64_t)cap) ? cap : (uint32_t)avail;
    hdr_out->start_seq = start;
    hdr_out->byte_len = count;
    if (!dst || count == 0u) {
        return;
    }
    const uint32_t off0 = (uint32_t)(start % (uint64_t)capacity);
    uint32_t first = capacity - off0;
    if (first > count) {
        first = count;
    }
    memcpy(dst, ring + off0, first);
    if (count > first) {
        memcpy(dst + first, ring, count - first);
    }
}

/* Box-filter downsample one output row `out_y` from BGRA8 framebuffer (`scale` in {1, 2, 4}). */
static inline void zizhan_dbg_downsample_bgra_row(uint8_t *dst_row,
                                                  const volatile uint8_t *src_base,
                                                  uint32_t src_stride,
                                                  uint32_t src_w, uint32_t src_h,
                                                  uint32_t out_y, uint32_t scale) {
    if (scale != 2u && scale != 4u) {
        scale = 1u;
    }
    const uint32_t out_w = src_w / scale;
    const uint32_t sy0 = out_y * scale;
    if (!dst_row || !src_base || sy0 + scale > src_h) {
        return;
    }
    if (scale == 1u) {
        const volatile uint8_t *s = src_base + (size_t)sy0 * src_stride;
        for (size_t i = 0; i < (size_t)out_w * 4u; ++i) {
            dst_row[i] = s[i];
        }
        return;
    }
    const uint32_t shift = (scale == 2u) ? 2u : 4u;
    const uint32_t half = 1u << (shift - 1u);
    for (uint32_t ox = 0; ox < out_w; ++ox) {
        uint32_t b = 0, g = 0, r = 0;
        const uint32_t sx0 = ox * scale;
        for (uint32_t dy = 0; dy < scale; ++dy) {
            const volatile uint8_t *p = src_base + (size_t)(sy0 + dy) * src_stride + (size_t)sx0 * 4u;
            for (uint32_t dx = 0; dx < scale; ++dx) {
                b += p[dx * 4u + 0u];
                g += p[dx * 4u + 1u];
                r += p[dx * 4u + 2u];
            }
        }
        dst_row[ox * 4u + 0u] = (uint8_t)((b + half) >> shift);
        dst_row[ox * 4u + 1u] = (uint8_t)((g + half) >> shift);
        dst_row[ox * 4u + 2u] = (uint8_t)((r + half) >> shift);
        dst_row[ox * 4u + 3u] = 0xffu;
    }
}

/* Hot-UI crash-rollback state machine shared by kern_exec.c and ASan host tests. */
typedef struct {
    uint32_t state;               /* ZizhanHotUiMode: 0=stock, 1=confirmed, 2=trial */
    uint32_t sb_trials;           /* SpringBoard execs while in TRIAL */
    uint32_t sb_execs;            /* SpringBoard execs since last ARM */
    uint32_t rollbacks;           /* total automatic rollbacks */
    uint32_t intentional_restart; /* 1 when SIGNAL_SB explicitly requested a clean respring */
    uint32_t active_slot;         /* 0 or 1 (/private/var/db/zizhan-hot-ui-{0,1}.dylib) */
    uint32_t staged_slot;         /* inactive slot prepared by stage_ui, activated on ARM */
    uint32_t staged_valid;        /* 1 when staged_slot holds a freshly unlinked+created dylib */
    uint64_t last_sb_exec_ms;     /* monotonic ms of previous SpringBoard exec */
} ZizhanHotUiTracker;

static inline uint32_t zizhan_hot_ui_pick_stage_slot(const ZizhanHotUiTracker *t) {
    if (!t || t->state == ZIZHAN_HOT_UI_STATE_STOCK) {
        return 0u;
    }
    return (t->active_slot & 1u) ^ 1u;
}

static inline void zizhan_hot_ui_mark_staged(ZizhanHotUiTracker *t, uint32_t slot, int valid) {
    if (!t) {
        return;
    }
    t->staged_slot = slot & 1u;
    t->staged_valid = valid ? 1u : 0u;
}

static inline int zizhan_hot_ui_is_eligible_proc(const char *name, size_t namelen, int is_app) {
    if (!name || namelen == 0u) {
        return 0;
    }
    /* Never override backboardd (hosts zizhand!) or core storage/security daemons. */
    if ((namelen == 10u && memcmp(name, "backboardd", 10u) == 0) ||
        (namelen == 8u && memcmp(name, "installd", 8u) == 0) ||
        (namelen == 3u && memcmp(name, "lsd", 3u) == 0) ||
        (namelen == 3u && memcmp(name, "pkd", 3u) == 0) ||
        (namelen == 12u && memcmp(name, "usermanagerd", 12u) == 0) ||
        (namelen == 13u && memcmp(name, "runningboardd", 13u) == 0) ||
        (namelen >= 17u && memcmp(name, "containermanagerd", 17u) == 0)) {
        return 0;
    }
    if ((namelen == 11u && memcmp(name, "SpringBoard", 11u) == 0) ||
        (namelen == 11u && memcmp(name, "Preferences", 11u) == 0) ||
        (namelen == 11u && memcmp(name, "PosterBoard", 11u) == 0) ||
        (namelen == 6u && memcmp(name, "Photos", 6u) == 0) ||
        is_app != 0) {
        return 1;
    }
    return 0;
}

/* Called under lock on every exec matching `/pong/libpong-ui18.dylib`.
 * Returns 1 if caller should substitute active hot-UI slot, 0 to keep stock `/pong/libpong-ui18.dylib`. */
static inline int zizhan_hot_ui_step_exec(ZizhanHotUiTracker *t, const char *name,
                                          size_t namelen, int is_app, uint64_t now_ms) {
    if (!t || t->state == ZIZHAN_HOT_UI_STATE_STOCK) {
        return 0;
    }
    if (!zizhan_hot_ui_is_eligible_proc(name, namelen, is_app)) {
        return 0;
    }
    const int is_sb = (namelen == 11u && memcmp(name, "SpringBoard", 11u) == 0) ? 1 : 0;
    if (t->state == ZIZHAN_HOT_UI_STATE_TRIAL) {
        if (!is_sb) {
            return 0;
        }
        if (t->sb_trials == 0u) {
            t->sb_trials = 1u;
            t->sb_execs += 1u;
            t->intentional_restart = 0u;
            t->last_sb_exec_ms = now_ms;
            return 1;
        }
        /* Any 2nd SpringBoard exec before CONFIRM (regardless of elapsed time) -> rollback! */
        t->state = ZIZHAN_HOT_UI_STATE_STOCK;
        t->sb_trials = 0u;
        t->intentional_restart = 0u;
        t->rollbacks += 1u;
        return 0;
    }
    if (t->state == ZIZHAN_HOT_UI_STATE_CONFIRMED) {
        if (is_sb) {
            if (t->intentional_restart != 0u) {
                t->intentional_restart = 0u;
                t->last_sb_exec_ms = now_ms;
                t->sb_execs += 1u;
                return 1;
            }
            if (t->last_sb_exec_ms != 0ull && now_ms >= t->last_sb_exec_ms &&
                (now_ms - t->last_sb_exec_ms) < 15000ull) {
                /* Unexpected rapid SpringBoard crash (< 15s from exec) -> rollback! */
                t->state = ZIZHAN_HOT_UI_STATE_STOCK;
                t->sb_trials = 0u;
                t->rollbacks += 1u;
                return 0;
            }
            t->last_sb_exec_ms = now_ms;
            t->sb_execs += 1u;
        }
        return 1;
    }
    return 0;
}

static inline uint32_t zizhan_hot_ui_ctl_step(ZizhanHotUiTracker *t, uint32_t cmd, uint64_t now_ms) {
    (void)now_ms;
    if (!t) {
        return 0u;
    }
    if (cmd == ZIZHAN_HOT_UI_REVERT) {
        t->state = ZIZHAN_HOT_UI_STATE_STOCK;
        t->sb_trials = 0u;
        t->intentional_restart = 0u;
        t->staged_valid = 0u;
        t->last_sb_exec_ms = 0ull;
    } else if (cmd == ZIZHAN_HOT_UI_ARM) {
        if (t->staged_valid == 0u) {
            return 0xffffffffu;
        }
        t->active_slot = t->staged_slot & 1u;
        t->staged_valid = 0u;
        t->state = ZIZHAN_HOT_UI_STATE_TRIAL;
        t->sb_trials = 0u;
        t->sb_execs = 0u;
        t->intentional_restart = 0u;
        t->last_sb_exec_ms = 0ull;
    } else if (cmd == ZIZHAN_HOT_UI_CONFIRM) {
        /* Preserve last_sb_exec_ms as actual exec timestamp so +5s confirm does not extend window! */
        if (t->state == ZIZHAN_HOT_UI_STATE_TRIAL || t->state == ZIZHAN_HOT_UI_STATE_CONFIRMED) {
            t->state = ZIZHAN_HOT_UI_STATE_CONFIRMED;
            t->sb_trials = 0u;
        }
    } else if (cmd == ZIZHAN_HOT_UI_NOTE_RESTART) {
        if (t->state == ZIZHAN_HOT_UI_STATE_CONFIRMED) {
            t->intentional_restart = 1u;
        }
    }
    return t->state;
}

/* --- Minimal RFC 2083 / RFC 1950 Stored-DEFLATE PNG Encoder (Zero external deps) --- */
static inline void zizhan_dbg_be32_put(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static inline uint32_t zizhan_dbg_crc32_step(uint32_t crc, const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint32_t)buf[i];
        for (unsigned b = 0; b < 8u; ++b) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xedb88320u) : (crc >> 1);
        }
    }
    return crc;
}

static inline size_t zizhan_dbg_png_stored_bound(uint32_t w, uint32_t h) {
    const size_t raw = (size_t)h * (1u + (size_t)w * 4u);
    const size_t blocks = (raw + 65534u) / 65535u;
    return 128u + raw + blocks * 5u;
}

static inline size_t zizhan_dbg_encode_png_stored(const uint8_t *bgra, uint32_t w, uint32_t h,
                                                  uint32_t stride, uint8_t *out, size_t out_cap) {
    if (!bgra || !out || w == 0u || h == 0u || w > 4096u || h > 4096u || stride < w * 4u) {
        return 0u;
    }
    const size_t need = zizhan_dbg_png_stored_bound(w, h);
    if (out_cap < need) {
        return 0u;
    }
    static const uint8_t kSig[8] = {0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au};
    memcpy(out, kSig, 8u);
    size_t pos = 8u;

    zizhan_dbg_be32_put(out + pos, 13u);
    memcpy(out + pos + 4u, "IHDR", 4u);
    zizhan_dbg_be32_put(out + pos + 8u, w);
    zizhan_dbg_be32_put(out + pos + 12u, h);
    out[pos + 16u] = 8u;
    out[pos + 17u] = 6u;
    out[pos + 18u] = 0u;
    out[pos + 19u] = 0u;
    out[pos + 20u] = 0u;
    const uint32_t ihdr_crc = ~zizhan_dbg_crc32_step(0xffffffffu, out + pos + 4u, 17u);
    zizhan_dbg_be32_put(out + pos + 21u, ihdr_crc);
    pos += 25u;

    const size_t idat_hdr = pos;
    memcpy(out + pos + 4u, "IDAT", 4u);
    pos += 8u;
    out[pos++] = 0x78u;
    out[pos++] = 0x01u;

    const size_t total_raw = (size_t)h * (1u + (size_t)w * 4u);
    size_t rem_raw = total_raw;
    uint32_t cur_y = 0u;
    uint32_t row_pos = 0u;
    uint32_t s1 = 1u, s2 = 0u;

    while (rem_raw > 0u) {
        const uint16_t blen = (rem_raw > 65535u) ? 65535u : (uint16_t)rem_raw;
        const uint8_t bfinal = (rem_raw == (size_t)blen) ? 1u : 0u;
        out[pos++] = bfinal;
        out[pos++] = (uint8_t)(blen & 0xffu);
        out[pos++] = (uint8_t)(blen >> 8);
        const uint16_t nlen = (uint16_t)~blen;
        out[pos++] = (uint8_t)(nlen & 0xffu);
        out[pos++] = (uint8_t)(nlen >> 8);

        uint16_t left = blen;
        while (left > 0u) {
            if (row_pos == 0u) {
                out[pos++] = 0u;
                s2 = (s2 + s1) % 65521u;
                row_pos = 1u;
                --left;
                continue;
            }
            const uint32_t px_byte_rem = w * 4u - (row_pos - 1u);
            uint32_t take = (left < px_byte_rem) ? left : px_byte_rem;
            const uint8_t *src_row = bgra + (size_t)cur_y * stride;
            for (uint32_t i = 0; i < take; ++i) {
                const uint32_t rb = (row_pos - 1u) + i;
                const uint32_t px = rb >> 2;
                const uint32_t ch = rb & 3u;
                uint8_t v;
                if (ch == 0u) {
                    v = src_row[px * 4u + 2u];
                } else if (ch == 1u) {
                    v = src_row[px * 4u + 1u];
                } else if (ch == 2u) {
                    v = src_row[px * 4u + 0u];
                } else {
                    v = 0xffu;
                }
                out[pos++] = v;
                s1 += v;
                if (s1 >= 65521u) {
                    s1 -= 65521u;
                }
                s2 += s1;
                if (s2 >= 65521u) {
                    s2 -= 65521u;
                }
            }
            row_pos += take;
            left = (uint16_t)(left - take);
            if (row_pos == 1u + w * 4u) {
                row_pos = 0u;
                ++cur_y;
            }
        }
        rem_raw -= blen;
    }

    zizhan_dbg_be32_put(out + pos, (s2 << 16) | s1);
    pos += 4u;

    const uint32_t idat_len = (uint32_t)(pos - (idat_hdr + 8u));
    zizhan_dbg_be32_put(out + idat_hdr, idat_len);
    const uint32_t idat_crc = ~zizhan_dbg_crc32_step(0xffffffffu, out + idat_hdr + 4u, 4u + (size_t)idat_len);
    zizhan_dbg_be32_put(out + pos, idat_crc);
    pos += 4u;

    zizhan_dbg_be32_put(out + pos, 0u);
    memcpy(out + pos + 4u, "IEND", 4u);
    const uint32_t iend_crc = ~zizhan_dbg_crc32_step(0xffffffffu, out + pos + 4u, 4u);
    zizhan_dbg_be32_put(out + pos + 8u, iend_crc);
    pos += 12u;
    return pos;
}

#ifdef __cplusplus
}
#endif

#endif /* ZIZHAN_DEBUG_PROTO_H */
