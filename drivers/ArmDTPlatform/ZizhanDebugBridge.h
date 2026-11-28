/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* zizhan DBG-01: Kernel-side live debug bridge (framebuffer downsample snapshot,
 * dynamic watchdog lease keepalive, clean fastboot/Android reboot, inner/outer
 * touch & button injection, Hall lid override, thread sampler trigger, and
 * SpringBoard hot-UI crash-rollback control).
 *
 * Registered into XNU's `zizhan_dbg_register()` (patches/xnu-ipados18/0049), which
 * gates every `kern.zizhan_dbg_*` sysctl behind `zizhan_debugd=1` AND (`uid == 0`
 * or platform-binary `backboardd` / `SpringBoard`).
 */
#pragma once

#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include <sys/errno.h>
#include "PongBootReset.h"
#include "ZizhanHallProbe.h"
#include "ZizhanOuterProbe.h"
#include "../../userland/zizhan-debug-proto.h"

extern "C" {
typedef int (*zizhan_dbg_emit_fn)(void *ctx, const void *data, size_t len);
typedef int (*zizhan_dbg_fb_fn)(const void *req_buf, size_t req_len,
                                zizhan_dbg_emit_fn emit_fn, void *emit_ctx, size_t *total_out);
typedef int (*zizhan_dbg_ctl_fn)(const void *req_buf, size_t req_len,
                                 void *resp_buf, size_t *resp_len);
void zizhan_dbg_register(zizhan_dbg_fb_fn fb_fn, zizhan_dbg_ctl_fn ctl_fn);
uint32_t zizhan_dbg_hot_ui_ctl(uint32_t cmd, uint32_t *rollbacks_out, uint32_t *sb_execs_out);
int zizhan_dbg_signal_springboard(int sig);
int pong_thread_sampler_trigger(const char *proc, unsigned seconds) __attribute__((weak));

uint32_t pong_scanout_cover_snapshot(const uint8_t **va_out, uint64_t *phys_out,
                                     uint32_t *w_out, uint32_t *h_out, uint32_t *stride_out);
int zizhan_display_power_state(void) __attribute__((weak));
int pong_battery_gauge_snapshot(int32_t *soc_pct, int32_t *vbatt_mv,
                                int32_t *ibatt_ma, uint32_t *charging_out);
extern volatile uint32_t pong_cpu_thermal_level;
extern volatile uint32_t pong_gpu_thermal_level;
extern volatile uint32_t pong_zc_flipped_tag;
extern int (*zizhan_power_button_hook)(int down);
size_t pong_ramoops_read_console(uint64_t since_seq, void *dst, size_t cap,
                                 uint64_t *total_out, uint64_t *start_out, uint32_t *dropped_out);
size_t pong_ramoops_read_pmsg(uint64_t since_seq, void *dst, size_t cap,
                              uint64_t *total_out, uint64_t *start_out);
}

namespace zizhan_debug_bridge {

static IOLock *gInputLock;

static uint64_t uptimeMs() {
  uint64_t ns = 0;
  absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
  return ns / 1000000ULL;
}

static uint32_t clampNorm65535(int32_t px, uint32_t span) {
  if (span <= 1u || px <= 0) {
    return 0u;
  }
  const uint32_t clamped = static_cast<uint32_t>(px) >= span ? (span - 1u) : static_cast<uint32_t>(px);
  return static_cast<uint32_t>((uint64_t(clamped) * 65535ULL) / uint64_t(span - 1u));
}

static void fillStatus(ZizhanDbgStatusResp &st, int32_t op_rc) {
  memset(&st, 0, sizeof(st));
  st.uptime_ms = uptimeMs();
  uint64_t klog_tot = 0, pmsg_tot = 0;
  (void)pong_ramoops_read_console(0, nullptr, 0, &klog_tot, nullptr, nullptr);
  (void)pong_ramoops_read_pmsg(0, nullptr, 0, &pmsg_tot, nullptr);
  st.klog_total_bytes = klog_tot;
  st.pmsg_bytes = static_cast<uint32_t>(pmsg_tot > 0xffffffffULL ? 0xffffffffU : pmsg_tot);

  uint32_t pings = 0;
  st.autoreset_rem_s = pong_boot_reset::remainingSeconds(&pings);
  st.autoreset_pings = pings;

  int32_t hall_closed = -1, hall_ovr = -1;
  zizhan_hall_get_state(&hall_closed, &hall_ovr);
  st.lid_closed = hall_closed;
  st.hall_override = hall_ovr;

  uint32_t ow = 1080u, oh = 2520u, ost = 4320u, oview = 0u;
  (void)zizhan_outer_fb_snapshot(nullptr, &ow, &oh, &ost, &oview);
  st.outer_active_view = oview;
  st.outer_w = ow;
  st.outer_h = oh;

  const uint8_t *iva = nullptr;
  uint64_t iphys = 0;
  uint32_t iw = 2160u, ih = 1916u, ist = 0u;
  (void)pong_scanout_cover_snapshot(&iva, &iphys, &iw, &ih, &ist);
  st.inner_w = iw ? iw : 2160u;
  st.inner_h = ih ? ih : 1916u;

  st.inner_display_pwr = zizhan_display_power_state ? zizhan_display_power_state() : -1;
  st.zc_flipped_tag = __atomic_load_n(&pong_zc_flipped_tag, __ATOMIC_ACQUIRE);

  int32_t soc = -1, mv = 0, ma = 0;
  uint32_t chg = 0;
  if (pong_battery_gauge_snapshot(&soc, &mv, &ma, &chg) == 0) {
    st.batt_soc_pct = soc;
    st.batt_mv = mv;
    st.batt_ma = ma;
    st.batt_charging = chg;
  } else {
    st.batt_soc_pct = -1;
  }

  st.cpu_thermal_lvl = __atomic_load_n(&pong_cpu_thermal_level, __ATOMIC_ACQUIRE);
  st.gpu_thermal_lvl = __atomic_load_n(&pong_gpu_thermal_level, __ATOMIC_ACQUIRE);

  uint32_t rollbacks = 0, sb_execs = 0;
  st.hot_ui_state = zizhan_dbg_hot_ui_ctl(ZIZHAN_HOT_UI_QUERY, &rollbacks, &sb_execs);
  st.hot_ui_rollbacks = rollbacks;
  st.hot_ui_sb_execs = sb_execs;
  st.op_result = op_rc;
}

static int fbSnapshot(const void *req_buf, size_t req_len,
                      zizhan_dbg_emit_fn emit_fn, void *emit_ctx, size_t *total_out) {
  ZizhanDbgFbReq req{};
  req.panel = ZIZHAN_DBG_PANEL_AUTO;
  req.scale = 2u;
  if (req_buf && req_len != 0u) {
    if (req_len != sizeof(req)) {
      return EINVAL;
    }
    memcpy(&req, req_buf, sizeof(req));
  }
  uint32_t scale = req.scale ? req.scale : 2u;
  if (scale != 1u && scale != 2u && scale != 4u) {
    return EINVAL;
  }

  uint32_t want_panel = req.panel;
  if (want_panel == ZIZHAN_DBG_PANEL_AUTO) {
    int32_t hall_closed = -1, hall_ovr = -1;
    zizhan_hall_get_state(&hall_closed, &hall_ovr);
    uint32_t oview = 0u;
    (void)zizhan_outer_fb_snapshot(nullptr, nullptr, nullptr, nullptr, &oview);
    want_panel = (hall_closed == 1 && oview != 0u) ? ZIZHAN_DBG_PANEL_OUTER : ZIZHAN_DBG_PANEL_INNER;
  }

  const volatile uint8_t *src_va = nullptr;
  uint32_t src_w = 0u, src_h = 0u, src_stride = 0u, seq = 0u, actual_panel = 0u;
  if (want_panel == ZIZHAN_DBG_PANEL_INNER) {
    const uint8_t *iva = nullptr;
    uint64_t iphys = 0;
    seq = pong_scanout_cover_snapshot(&iva, &iphys, &src_w, &src_h, &src_stride);
    src_va = iva;
    actual_panel = ZIZHAN_DBG_PANEL_INNER;
  } else if (want_panel == ZIZHAN_DBG_PANEL_OUTER) {
    uint32_t oview = 0u;
    seq = zizhan_outer_fb_snapshot(&src_va, &src_w, &src_h, &src_stride, &oview);
    actual_panel = ZIZHAN_DBG_PANEL_OUTER;
  } else {
    return EINVAL;
  }

  if (!src_va || src_w < 64u || src_w > 4096u || src_h < 64u || src_h > 4096u ||
      src_stride < src_w * 4u || src_stride > 16384u) {
    return EAGAIN;
  }

  const uint32_t out_w = src_w / scale;
  const uint32_t out_h = src_h / scale;
  const uint32_t out_stride = out_w * 4u;
  const size_t total = sizeof(ZizhanDbgFbHdr) + size_t(out_stride) * size_t(out_h);
  if (total_out) {
    *total_out = total;
  }
  if (!emit_fn) {
    return 0;
  }

  ZizhanDbgFbHdr hdr{};
  hdr.width = out_w;
  hdr.height = out_h;
  hdr.stride = out_stride;
  hdr.format = 1u;
  hdr.panel = actual_panel;
  hdr.seq = seq;
  hdr.src_width = src_w;
  hdr.src_height = src_h;

  int err = emit_fn(emit_ctx, &hdr, sizeof(hdr));
  if (err) {
    return err;
  }

  const size_t kChunkCap = 16384u;
  uint8_t *chunk = static_cast<uint8_t *>(IOMallocData(kChunkCap));
  if (!chunk) {
    return ENOMEM;
  }
  size_t buffered = 0u;
  for (uint32_t y = 0; y < out_h; ++y) {
    if (buffered + out_stride > kChunkCap) {
      err = emit_fn(emit_ctx, chunk, buffered);
      buffered = 0u;
      if (err) {
        break;
      }
    }
    zizhan_dbg_downsample_bgra_row(chunk + buffered, src_va, src_stride, src_w, src_h, y, scale);
    buffered += out_stride;
  }
  if (!err && buffered > 0u) {
    err = emit_fn(emit_ctx, chunk, buffered);
  }
  IOFreeData(chunk, kChunkCap);
  return err;
}

static int injectTouch(const ZizhanDbgCtlReq &req) {
  if (!gInputLock) {
    return ENOMEM;
  }
  uint32_t panel = req.target;
  if (panel == ZIZHAN_DBG_PANEL_AUTO) {
    int32_t hall_closed = -1, hall_ovr = -1;
    zizhan_hall_get_state(&hall_closed, &hall_ovr);
    uint32_t oview = 0u;
    (void)zizhan_outer_fb_snapshot(nullptr, nullptr, nullptr, nullptr, &oview);
    panel = (hall_closed == 1 && oview != 0u) ? ZIZHAN_DBG_PANEL_OUTER : ZIZHAN_DBG_PANEL_INNER;
  }

  const bool is_swipe = (req.steps > 0u) || (req.x0 != req.x1) || (req.y0 != req.y1);
  uint32_t dur_ms = req.duration_ms;
  if (dur_ms == 0u) {
    dur_ms = is_swipe ? 180u : 55u;
  }
  if (dur_ms < 20u) { dur_ms = 20u; }
  if (dur_ms > 2000u) { dur_ms = 2000u; }
  uint32_t steps = req.steps;
  if (is_swipe && steps == 0u) {
    steps = dur_ms / 16u;
    if (steps < 4u) { steps = 4u; }
  }
  if (steps > 80u) { steps = 80u; }

  if (!IOLockTryLock(gInputLock)) {
    return EBUSY;
  }
  const auto clampPt = [](int32_t v, int32_t hi) -> int32_t {
    if (v < 0) { return 0; }
    if (v > hi) { return hi; }
    return v;
  };
  int rc = 0;
  if (panel == ZIZHAN_DBG_PANEL_OUTER) {
    const int32_t ox0 = clampPt(req.x0, 1079), oy0 = clampPt(req.y0, 2519);
    const int32_t ox1 = clampPt(is_swipe ? req.x1 : req.x0, 1079), oy1 = clampPt(is_swipe ? req.y1 : req.y0, 2519);
    rc = zizhan_outer_inject_touch(0x13u, static_cast<uint32_t>(ox0), static_cast<uint32_t>(oy0));
    if (rc == 0) {
      if (is_swipe && steps >= 1u) {
        const uint32_t step_sleep = dur_ms / steps ? (dur_ms / steps) : 10u;
        for (uint32_t s = 1u; s <= steps; ++s) {
          IOSleep(step_sleep);
          const int32_t mx = ox0 + static_cast<int32_t>(((int64_t(ox1) - int64_t(ox0)) * int64_t(s)) / int64_t(steps));
          const int32_t my = oy0 + static_cast<int32_t>(((int64_t(oy1) - int64_t(oy0)) * int64_t(s)) / int64_t(steps));
          (void)zizhan_outer_inject_touch(0x23u, static_cast<uint32_t>(mx), static_cast<uint32_t>(my));
        }
        IOSleep(16);
        (void)zizhan_outer_inject_touch(0x33u, static_cast<uint32_t>(ox1), static_cast<uint32_t>(oy1));
      } else {
        IOSleep(dur_ms);
        (void)zizhan_outer_inject_touch(0x33u, static_cast<uint32_t>(ox0), static_cast<uint32_t>(oy0));
      }
    }
  } else if (panel == ZIZHAN_DBG_PANEL_INNER || panel == ZIZHAN_DBG_PANEL_NORM) {
    uint32_t iw = 2160u, ih = 1916u;
    if (panel == ZIZHAN_DBG_PANEL_INNER) {
      const uint8_t *iva = nullptr;
      uint64_t iphys = 0;
      uint32_t ist = 0u;
      (void)pong_scanout_cover_snapshot(&iva, &iphys, &iw, &ih, &ist);
      if (!iw) { iw = 2160u; }
      if (!ih) { ih = 1916u; }
    } else {
      iw = 65536u;
      ih = 65536u;
    }
    const uint32_t nx0 = clampNorm65535(req.x0, iw);
    const uint32_t ny0 = clampNorm65535(req.y0, ih);
    const uint32_t nx1 = clampNorm65535(is_swipe ? req.x1 : req.x0, iw);
    const uint32_t ny1 = clampNorm65535(is_swipe ? req.y1 : req.y0, ih);
    /* Finger 8 is dedicated to remote debug touches (0..4 hardware, 9 outer Home bar). */
    pong_touch_post(1u, 8u, nx0, ny0);
    if (is_swipe && steps >= 1u) {
      const uint32_t step_sleep = dur_ms / steps ? (dur_ms / steps) : 10u;
      for (uint32_t s = 1u; s <= steps; ++s) {
        IOSleep(step_sleep);
        const uint32_t mx = static_cast<uint32_t>(int64_t(nx0) + (int64_t(nx1) - int64_t(nx0)) * int64_t(s) / int64_t(steps));
        const uint32_t my = static_cast<uint32_t>(int64_t(ny0) + (int64_t(ny1) - int64_t(ny0)) * int64_t(steps ? s : 1u) / int64_t(steps));
        pong_touch_post(2u, 8u, mx, my);
      }
      IOSleep(14);
      pong_touch_post(3u, 8u, nx1, ny1);
    } else {
      IOSleep(dur_ms);
      pong_touch_post(3u, 8u, nx0, ny0);
    }
  } else {
    rc = EINVAL;
  }
  IOLockUnlock(gInputLock);
  return rc;
}

static int injectKey(const ZizhanDbgCtlReq &req) {
  if (!gInputLock) {
    return ENOMEM;
  }
  uint32_t hold_ms = req.duration_ms ? req.duration_ms : 80u;
  if (hold_ms < 30u) { hold_ms = 30u; }
  if (hold_ms > 1500u) { hold_ms = 1500u; } /* keep well below 3000ms button-reset */

  if (!IOLockTryLock(gInputLock)) {
    return EBUSY;
  }
  int rc = 0;
  if (req.target == ZIZHAN_DBG_KEY_POWER) {
    const bool consumed_down = (zizhan_power_button_hook != nullptr) && (zizhan_power_button_hook(1) != 0);
    if (!consumed_down) {
      pong_touch_post(4u, 0u, 0u, 0u);
    }
    IOSleep(hold_ms);
    const bool consumed_up = (zizhan_power_button_hook != nullptr) && (zizhan_power_button_hook(0) != 0);
    if (!consumed_down && !consumed_up) {
      pong_touch_post(5u, 0u, 0u, 0u);
    }
  } else if (req.target == ZIZHAN_DBG_KEY_VOL_UP || req.target == ZIZHAN_DBG_KEY_VOL_DOWN) {
    const uint32_t code = (req.target == ZIZHAN_DBG_KEY_VOL_UP) ? 1u : 2u;
    pong_touch_post(4u, code, 0u, 0u);
    IOSleep(hold_ms > 200u ? 80u : hold_ms);
    pong_touch_post(5u, code, 0u, 0u);
  } else if (req.target == ZIZHAN_DBG_KEY_HOME) {
    pong_touch_post(1u, 9u, 32768u, 65200u);
    IOSleep(18);
    pong_touch_post(2u, 9u, 32768u, 55000u);
    IOSleep(18);
    pong_touch_post(2u, 9u, 32768u, 42000u);
    IOSleep(18);
    pong_touch_post(2u, 9u, 32768u, 28000u);
    IOSleep(12);
    pong_touch_post(3u, 9u, 32768u, 28000u);
  } else {
    rc = EINVAL;
  }
  IOLockUnlock(gInputLock);
  return rc;
}

static int ctlDispatch(const void *req_buf, size_t req_len, void *resp_buf, size_t *resp_len) {
  ZizhanDbgCtlReq req{};
  req.op = ZIZHAN_DBG_OP_STATUS;
  if (req_buf && req_len != 0u) {
    if (req_len != sizeof(req)) {
      return EINVAL;
    }
    memcpy(&req, req_buf, sizeof(req));
    req.text[sizeof(req.text) - 1u] = '\0';
  }

  int sys_err = 0;
  int32_t op_res = 0;
  switch (req.op) {
    case ZIZHAN_DBG_OP_STATUS:
      break;
    case ZIZHAN_DBG_OP_EXTEND_WDT: {
      const uint32_t lease = req.duration_ms ? req.duration_ms : 300u;
      op_res = pong_boot_reset::extendLease(lease, req.y0 != 0);
      break;
    }
    case ZIZHAN_DBG_OP_REBOOT: {
      if (req.target != ZIZHAN_DBG_REBOOT_FASTBOOT && req.target != ZIZHAN_DBG_REBOOT_ANDROID) {
        sys_err = EINVAL;
      } else {
        op_res = pong_boot_reset::triggerRemoteReboot(req.target == ZIZHAN_DBG_REBOOT_ANDROID);
        if (op_res != 0) {
          sys_err = EBUSY;
        }
      }
      break;
    }
    case ZIZHAN_DBG_OP_TOUCH:
      sys_err = injectTouch(req);
      op_res = sys_err;
      break;
    case ZIZHAN_DBG_OP_KEY:
      sys_err = injectKey(req);
      op_res = sys_err;
      break;
    case ZIZHAN_DBG_OP_HALL_OVERRIDE:
      if (req.x0 < -1 || req.x0 > 1) {
        sys_err = EINVAL;
      } else {
        zizhan_hall_set_override(req.x0);
        op_res = req.x0;
      }
      break;
    case ZIZHAN_DBG_OP_SAMPLE_PROC: {
      if (!pong_thread_sampler_trigger && !pong_thread_sampler_start) {
        sys_err = ENOTSUP;
      } else {
        char proc[32] = "SpringBoard";
        if (req.text[0] != '\0') {
          strlcpy(proc, req.text, sizeof(proc));
        }
        uint32_t secs = req.duration_ms ? req.duration_ms : 3u;
        if (secs < 1u) { secs = 1u; }
        if (secs > 15u) { secs = 15u; }
        if (pong_thread_sampler_trigger) {
          sys_err = pong_thread_sampler_trigger(proc, secs);
        } else {
          pong_thread_sampler_start(proc, secs);
        }
        op_res = (sys_err == 0) ? static_cast<int32_t>(secs) : -sys_err;
      }
      break;
    }
    case ZIZHAN_DBG_OP_HOT_UI: {
      if (req.target > ZIZHAN_HOT_UI_NOTE_RESTART) {
        sys_err = EINVAL;
      } else {
        const uint32_t raw_st = zizhan_dbg_hot_ui_ctl(req.target, nullptr, nullptr);
        if (raw_st == 0xffffffffu) {
          sys_err = ENOENT;
          op_res = -ENOENT;
        } else {
          op_res = static_cast<int32_t>(raw_st);
        }
      }
      break;
    }
    case ZIZHAN_DBG_OP_SIGNAL_SB: {
      const int sig = (req.target == 9u) ? 9 : 15; /* SIGKILL=9, SIGTERM=15 */
      if (sig == 15) {
        (void)zizhan_dbg_hot_ui_ctl(ZIZHAN_HOT_UI_NOTE_RESTART, nullptr, nullptr);
      }
      op_res = zizhan_dbg_signal_springboard(sig);
      if (op_res < 0) {
        sys_err = ESRCH;
      }
      break;
    }
    default:
      sys_err = EINVAL;
      break;
  }

  if (resp_buf && resp_len && *resp_len >= sizeof(ZizhanDbgStatusResp)) {
    ZizhanDbgStatusResp st{};
    fillStatus(st, op_res);
    memcpy(resp_buf, &st, sizeof(st));
    *resp_len = sizeof(st);
  } else if (resp_len) {
    *resp_len = sizeof(ZizhanDbgStatusResp);
  }
  return sys_err;
}

static void start() {
  if (!gInputLock) {
    gInputLock = IOLockAlloc();
  }
  zizhan_dbg_register(fbSnapshot, ctlDispatch);
  uint32_t en = 0;
  if (PE_parse_boot_argn("zizhan_debugd", &en, sizeof(en)) && en != 0u) {
    IOLog("ZIZHAN_DEBUGD: kernel bridge registered (zizhan_debugd=%u)\n", en);
  }
}

}  // namespace zizhan_debug_bridge
