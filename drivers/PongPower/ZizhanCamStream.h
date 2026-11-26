/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Track 3A (CAM-02): On-demand continuous MIPI CSI-2 camera streaming service for Xiaomi MIX Fold 2.
 * Supports:
 *  - Rear Ultra-Wide OV13B10 (CSIPHY1 0xace6000, CCI0 m1 0x36, 1364x768 RAW10 -> 640x480 BGRA8888)
 *  - Front Selfie IMX596 (CSIPHY3 0xacea000, CCI0 m1 0x10/0x1a, 1920x1080 RAW10 -> 640x480 BGRA8888)
 *  - Closed-loop real-time Auto-Exposure (AE) + Gray-World Auto-White-Balance (AWB) + gamma 2.0 ISP
 *  - Crash-proof 3.5-second userland heartbeat lease: 0 mW full reverse teardown when idle/closed. */
#pragma once
#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <sys/errno.h>
#include "ZizhanCamCapture.h"
#include "ZizhanImx596Regs.h"
#include "../../userland/zizhan-cam-proto.h"

extern "C" {
typedef int (*zizhan_cam_ctrl_fn)(int);
typedef size_t (*zizhan_cam_frame_fn)(void *, size_t, size_t);
void zizhan_cam_register(zizhan_cam_ctrl_fn, zizhan_cam_frame_fn);
int zizhan_cam_sleep_busy(void);
}

namespace zizhan_cam_stream {

using zizhan_cam_capture::MmioWin;
using zizhan_cam_capture::SmmuCamCtx;
using zizhan_cam_capture::PmI2cCtx;
using zizhan_cam_capture::kCamCarveoutPa;
using zizhan_cam_capture::kCamCarveoutSize;
using zizhan_cam_capture::kTpgSlotIova;
using zizhan_cam_capture::kOvSlot0Iova;
using zizhan_cam_capture::kOvSlot1Iova;

static IOLock *gLock = nullptr;
static bool gThreadRunning = false;
static volatile int gDesiredMode = 0;   /* 0=off, 1=back, 2=front, 3=back+qr */
static volatile int gActiveSensor = 0;  /* 0=off, 1=back_ov13b10, 2=front_imx596 */
static volatile uint64_t gLeaseUntilTicks = 0;
static uint8_t *gPublishedBuf[2] = {nullptr, nullptr};
static volatile uint32_t gPublishedIdx = 0;
static volatile uint32_t gPublishedSeq = 0;

static uint64_t readCntvct() {
  uint64_t v = 0;
  __asm__ volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(v));
  return v;
}
static uint64_t readCntfrq() {
  uint64_t f = 0;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
  return f ? f : 19200000ull;
}

/* Enable/disable CSIPHY3 clocks in CAM_CC (csi3phytimer RCG 0x1514c / CBCR 0x15164, csiphy3 CBCR 0x15168). */
static int camccCsiphy3(const ZizhanCamCcOps *cc, bool on) {
  if (on) {
    if (zizhan_camcc_rcg_cfg(cc, 0x1514cu, 3u, 1u)) return -1;
    if (zizhan_camcc_branch(cc, 0x15164u, 1)) return -2;
    if (zizhan_camcc_branch(cc, 0x15168u, 1)) return -3;
    return 0;
  }
  (void)zizhan_camcc_branch(cc, 0x15168u, 0);
  (void)zizhan_camcc_branch(cc, 0x15164u, 0);
  (void)zizhan_camcc_rcg_cfg(cc, 0x1514cu, 0u, 1u);
  return 0;
}

struct StreamSession {
  int reqPos = 1;       /* 1 = back, 2 = front */
  int hwPos = 1;        /* actual sensor opened: 1 = OV13B10, 2 = IMX596 */
  uint8_t cciAddr = 0x36u;
  uint16_t rawW = ZIZHAN_CAM_OV13B10_W;
  uint16_t rawH = ZIZHAN_CAM_OV13B10_H;
  uint16_t rawStride = ZIZHAN_CAM_OV13B10_STRIDE;
  int bayerOrder = ZIZHAN_BAYER_GRBG;
  int addrShifted = 0;
  uint8_t csiphyId = 1u;

  MmioWin gccCam, vote, wrap, se, camcc, cci0, csiphy, tpg1, csid, vfe, carveout;
  MmioWin pm_en, vana, vdig, mclk, rst, pad0, pad1, i2c0, i2c1;
  uint32_t s_gcc_ahb = 0, s_gcc_hf = 0, s_gcc_sf = 0, s_gcc_xo = 0;
  uint32_t s_pm = 0, s_vana = 0, s_vdig = 0, s_mclk = 0, s_rst = 0, s_pad0 = 0, s_pad1 = 0, s_i2c0 = 0, s_i2c1 = 0;
  SmmuCamCtx smmuCtx;
  PmI2cCtx pmCtx{};
  bool havePmI2c = false, ispClksOn = false, phy3ClksOn = false, mclkOn = false, cciOn = false, streaming = false;
  uint32_t mclkRcg = 0x1501cu, mclkCbcr = 0x15034u;
  unsigned vdigLdo = 0;

  ZizhanCamAeState ae{};
  uint16_t awbR = 256u, awbB = 256u;

  bool openHardware(int wantPos) {
    reqPos = wantPos;
    hwPos = wantPos;
    const bool tryImx = (wantPos == 2);
    const uint64_t phyBase = tryImx ? 0xacea000ull : 0xace6000ull; /* CSIPHY3 vs CSIPHY1 */
    csiphyId = tryImx ? 3u : 1u;
    mclkRcg  = tryImx ? 0x1508cu : 0x1501cu; /* MCLK5 vs MCLK1 */
    mclkCbcr = tryImx ? 0x150a4u : 0x15034u;
    vdigLdo  = tryImx ? 2u : 0u;
    const unsigned vanaPin = tryImx ? 97u : 6u;
    const unsigned vdigPin = tryImx ? 92u : 92u;
    const unsigned mclkPin = tryImx ? 105u : 101u;
    const unsigned rstPin  = tryImx ? 127u : 24u;

    if (!gccCam.open(0x136000ull, 0x1000u) || !vote.open(0x162000ull, 0x1000u) ||
        !wrap.open(0x127000ull, 0x1000u) || !se.open(0x994000ull, 0x4000u) ||
        !camcc.open(0xade0000ull, 0x20000u) || !cci0.open(0xac15000ull, 0x1000u) ||
        !csiphy.open(phyBase, 0x2000u) || !tpg1.open(0xacf7000ull, 0x1000u) ||
        !csid.open(0xacc6000ull, 0x1000u) || !vfe.open(0xacc7000ull, 0x1800u) ||
        !carveout.open(kCamCarveoutPa, kCamCarveoutSize, kIOMapWriteCombineCache)) {
      IOLog("ZIZHAN_CAM_STREAM: FAIL mmio-map pos=%d\n", wantPos);
      return false;
    }
    auto pin_open = [](MmioWin &w, unsigned n) { return w.open(0xf100000ull + uint64_t(n) * 0x1000ull, 0x1000u); };
    if (!pin_open(pm_en, 11) || !pin_open(vana, vanaPin) || !pin_open(vdig, vdigPin) ||
        !pin_open(mclk, mclkPin) || !pin_open(rst, rstPin) ||
        !pin_open(pad0, 112) || !pin_open(pad1, 113) ||
        !pin_open(i2c0, 206) || !pin_open(i2c1, 207)) {
      IOLog("ZIZHAN_CAM_STREAM: FAIL pin-map pos=%d\n", wantPos);
      return false;
    }

    s_gcc_ahb = gccCam.r(0x004); s_gcc_hf = gccCam.r(0x010); s_gcc_sf = gccCam.r(0x018); s_gcc_xo = gccCam.r(0x020);
    s_pm = pm_en.r(0); s_vana = vana.r(0); s_vdig = vdig.r(0); s_mclk = mclk.r(0); s_rst = rst.r(0);
    s_pad0 = pad0.r(0); s_pad1 = pad1.r(0); s_i2c0 = i2c0.r(0); s_i2c1 = i2c1.r(0);

    zizhan_cam_capture::zeroCarveout(carveout.va, 0x600000u);
    gccCam.w(0x004, s_gcc_ahb | 1u);
    gccCam.w(0x010, s_gcc_hf | 1u);
    gccCam.w(0x018, s_gcc_sf | 1u);
    gccCam.w(0x020, s_gcc_xo | 1u);
    IODelay(200);
    if (gccCam.r(0x004) & 0x80000000u) return false;

    (void)zizhan_cam_capture::voteCsiphyLdos(true);
    if (!smmuCtx.setup()) {
      teardown();
      return false;
    }

    ZizhanCamCcOps ccOps   = {&camcc,  zizhan_cam_capture::mmio_rd, zizhan_cam_capture::mmio_wr, zizhan_cam_capture::mmio_pause, 2000};
    ZizhanCamCcOps phyOps  = {&csiphy, zizhan_cam_capture::mmio_rd, zizhan_cam_capture::mmio_wr, zizhan_cam_capture::mmio_pause, 2000};
    ZizhanCamCcOps tpgOps  = {&tpg1,   zizhan_cam_capture::mmio_rd, zizhan_cam_capture::mmio_wr, zizhan_cam_capture::mmio_pause, 2000};
    ZizhanCamCcOps csidOps = {&csid,   zizhan_cam_capture::mmio_rd, zizhan_cam_capture::mmio_wr, zizhan_cam_capture::mmio_pause, 2000};
    ZizhanCamCcOps vfeOps  = {&vfe,    zizhan_cam_capture::mmio_rd, zizhan_cam_capture::mmio_wr, zizhan_cam_capture::mmio_pause, 2000};
    ZizhanCciOps   cciOps  = {&cci0,   zizhan_cam_capture::mmio_rd, zizhan_cam_capture::mmio_wr, zizhan_cam_capture::mmio_pause, 3000};

    unsigned clkStage = 0;
    if (zizhan_camcc_isp_clocks_on(&ccOps, &clkStage) != ZizhanCamCcOk) {
      teardown();
      return false;
    }
    ispClksOn = true;
    if (tryImx) {
      if (camccCsiphy3(&ccOps, true) != 0) {
        teardown();
        return false;
      }
      phy3ClksOn = true;
    }

    /* Self-calibrate VFE-lite0 WM0 address shift (0 vs 1) with 20 ms TPG1 burst */
    addrShifted = 0;
    for (int stry = 0; stry <= 1; ++stry) {
      volatile uint8_t *tpgDst = carveout.va + (kTpgSlotIova - kCamCarveoutPa);
      zizhan_cam_capture::zeroCarveout(tpgDst, 4096u);
      (void)zizhan_csid_lite_reset(&csidOps);
      zizhan_vfe_lite_wm0_start(&vfeOps, kTpgSlotIova, 128u, 1712u, stry);
      zizhan_csid_lite_start_rdi0(&csidOps, 1u, 4u, 0x3210u, 1);
      (void)zizhan_tpg_start(&tpgOps, 1364u, 128u, 4u);
      uint32_t bd = 0;
      for (unsigned ms = 0; ms < 30; ++ms) {
        IOSleep(1);
        bd |= csid.r(0x8c);
        if (bd & 0x6u) break;
      }
      zizhan_tpg_stop(&tpgOps);
      zizhan_csid_lite_stop_rdi0(&csidOps);
      zizhan_vfe_lite_wm0_stop(&vfeOps);
      if (reinterpret_cast<const volatile uint32_t *>(tpgDst)[0] != 0u ||
          reinterpret_cast<const volatile uint32_t *>(tpgDst)[16] != 0u) {
        addrShifted = stry;
        break;
      }
    }

    /* Power up PM8008J + sensor rails + MCLK + CCI0 m1 */
    vote.w(0x8, vote.r(0x8) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 15));
    if (!(wrap.r(0x62c) & 1u)) {
      wrap.w(0x61c, (wrap.r(0x61c) & ~0x1037ffu) | 1u);
      wrap.w(0x618, wrap.r(0x618) | 1u);
    }
    for (unsigned i = 0; i < 500; ++i) {
      if (!(wrap.r(0x610) & 0x80000000u)) break;
      IODelay(50);
    }
    i2c0.w(0, (i2c0.r(0) & ~0x3ffu) | (1u << 2) | 3u);
    i2c1.w(0, (i2c1.r(0) & ~0x3ffu) | (1u << 2) | 3u);
    if (!(se.r(0x48) & 1u) || !se.r(0x278)) {
      se.w(0x48, (7u << 4) | 1u);
      se.w(0x278, (10u << 20) | (11u << 10) | 26u);
    }
    pmCtx.se = &se;
    const GeniIo gio = {zizhan_cam_capture::se_rd, zizhan_cam_capture::se_wr, &se};
    uint32_t i2cCfg = 0;
    if (geni_i2c_prepare_poll(&pmCtx.g, &gio, 0x0d, &i2cCfg) != 0) {
      teardown();
      return false;
    }
    havePmI2c = true;
    zizhan_cam_capture::gpio_drive(pm_en, 1);
    zizhan_cam_capture::gpio_drive(rst, 0);
    IOSleep(4);

    const ZizhanPm8008Ops pmOps = {&pmCtx, zizhan_cam_capture::pm_rd, zizhan_cam_capture::pm_wr};
    const ZizhanPm8008Rail vioRail = {5u, 1800u};
    unsigned railsDone = 0;
    if (zizhan_pm8008_rails_on(&pmOps, &vioRail, 1, &railsDone) != 0) {
      teardown();
      return false;
    }
    zizhan_cam_capture::gpio_drive(vana, 1);
    IOSleep(1);
    if (vdigLdo) {
      const ZizhanPm8008Rail digRail = {vdigLdo, 1152u};
      if (zizhan_pm8008_rails_on(&pmOps, &digRail, 1, &railsDone) != 0) {
        teardown();
        return false;
      }
    } else {
      zizhan_cam_capture::gpio_drive(vdig, 1);
      IOSleep(1);
    }

    unsigned mclkStage = 0;
    if (zizhan_camcc_rcg_xo(&ccOps, mclkRcg) != 0 || zizhan_camcc_branch(&ccOps, mclkCbcr, 1) != 0) {
      (void)mclkStage;
      teardown();
      return false;
    }
    mclkOn = true;
    mclk.w(0, (s_mclk & ~0x3ffu) | (1u << 2));
    IOSleep(2);
    zizhan_cam_capture::gpio_drive(rst, 1);
    IOSleep(10);

    if (zizhan_cci_clocks_on_for(&ccOps, 0x1312cu, 0x13144u) != 0) {
      teardown();
      return false;
    }
    cciOn = true;
    pad0.w(0, (s_pad0 & ~0x3ffu) | (1u << 2) | 3u);
    pad1.w(0, (s_pad1 & ~0x3ffu) | (1u << 2) | 3u);
    if (zizhan_cci_reset_init_m(&cciOps, 1u) != 0) {
      teardown();
      return false;
    }

    if (tryImx) {
      /* Probe IMX596 chip ID 0x0596 on CCI0 m1 (0x10 / 0x1a / 0x37) */
      const uint8_t cands[] = {0x10u, 0x1au, 0x37u};
      bool foundImx = false;
      for (uint8_t a : cands) {
        uint8_t id[2] = {0, 0};
        if (zizhan_cci_read16_m(&cciOps, 1u, a, 0x0016u, id, 2u) == 0 && id[0] == 0x05u && id[1] == 0x96u) {
          cciAddr = a;
          foundImx = true;
          break;
        }
        (void)zizhan_cci_reset_init_m(&cciOps, 1u);
      }
      if (!foundImx) {
        IOLog("ZIZHAN_CAM_STREAM: IMX596 ID probe miss, falling back cleanly\n");
        teardown();
        return false;
      }
      rawW = 1920u;
      rawH = 1080u;
      rawStride = 2400u; /* 1920 * 10 / 8 = 2400 (multiple of 16) */
      bayerOrder = ZIZHAN_BAYER_RGGB;
      zizhan_cam_ae_init(&ae, 900u, 1540u, 512u, 3840u);

      if (zizhan_cci_write16_table_m(&cciOps, 1u, cciAddr, kZizhanImx596InitRegs,
                                   sizeof(kZizhanImx596InitRegs) / sizeof(kZizhanImx596InitRegs[0]), nullptr) != 0 ||
          zizhan_cci_write16_table_m(&cciOps, 1u, cciAddr, kZizhanImx596Mode1920x1080,
                                   sizeof(kZizhanImx596Mode1920x1080) / sizeof(kZizhanImx596Mode1920x1080[0]), nullptr) != 0) {
        IOLog("ZIZHAN_CAM_STREAM: IMX596 table write failed\n");
        teardown();
        return false;
      }
      (void)zizhan_csiphy_dphy_enable(&phyOps, 672000000ull, 400000000u, 4u);
      (void)zizhan_csid_lite_reset(&csidOps);
      zizhan_vfe_lite_wm0_start(&vfeOps, kOvSlot0Iova, rawH, rawStride, addrShifted);
      zizhan_csid_lite_start_rdi0(&csidOps, csiphyId, 4u, 0x3210u, 0);
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x0100u, 0x01u);
    } else {
      cciAddr = ZIZHAN_CAM_OV13B10_ADDR7;
      uint8_t id[3] = {0, 0, 0};
      if (zizhan_cci_read16_m(&cciOps, 1u, cciAddr, 0x300au, id, 3u) != 0 || id[1] != 0x0du || id[2] != 0x42u) {
        IOLog("ZIZHAN_CAM_STREAM: OV13B10 ID mismatch %02x%02x%02x\n", id[0], id[1], id[2]);
        teardown();
        return false;
      }
      rawW = ZIZHAN_CAM_OV13B10_W;
      rawH = ZIZHAN_CAM_OV13B10_H;
      rawStride = ZIZHAN_CAM_OV13B10_STRIDE;
      bayerOrder = ZIZHAN_BAYER_GRBG;
      zizhan_cam_ae_init(&ae, 1100u, 1560u, 512u, 3840u);

      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x0103u, 0x01u);
      IOSleep(5);
      if (zizhan_cci_write16_table_m(&cciOps, 1u, cciAddr, kZizhanOv13b10Init1120 + 1,
                                   (sizeof(kZizhanOv13b10Init1120) / sizeof(kZizhanOv13b10Init1120[0])) - 1, nullptr) != 0 ||
          zizhan_cci_write16_table_m(&cciOps, 1u, cciAddr, kZizhanOv13b10Mode1364x768,
                                   sizeof(kZizhanOv13b10Mode1364x768) / sizeof(kZizhanOv13b10Mode1364x768[0]), nullptr) != 0) {
        teardown();
        return false;
      }
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x5080u, 0x00u);
      (void)zizhan_csiphy_dphy_enable(&phyOps, 560000000ull, 400000000u, 4u);
      (void)zizhan_csid_lite_reset(&csidOps);
      zizhan_vfe_lite_wm0_start(&vfeOps, kOvSlot0Iova, rawH, rawStride, addrShifted);
      zizhan_csid_lite_start_rdi0(&csidOps, csiphyId, 4u, 0x3210u, 0);
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x0100u, 0x01u);
    }

    streaming = true;
    /* Verify at least 1 real MIPI CSI-2 frame lands within 250 ms when opening IMX596 so we can fallback if needed */
    if (tryImx) {
      bool gotFirst = false;
      for (unsigned ms = 0; ms < 250; ++ms) {
        IOSleep(1);
        if (csid.r(0x8c) & 0x6u) { gotFirst = true; break; }
      }
      if (!gotFirst) {
        IOLog("ZIZHAN_CAM_STREAM: IMX596 CSIPHY3 rx_irq=0x%x buf_done=0x%x -> fallback to mirrored OV13B10\n",
              csid.r(0x9c), csid.r(0x8c));
        teardown();
        return false;
      }
    }

    IOLog("ZIZHAN_CAM_STREAM: OPEN PASS req_pos=%d hw_pos=%d res=%ux%u csiphy=%u addr=0x%x\n",
          reqPos, hwPos, rawW, rawH, csiphyId, cciAddr);
    return true;
  }

  void applySensorAe() {
    ZizhanCciOps cciOps = {&cci0, zizhan_cam_capture::mmio_rd, zizhan_cam_capture::mmio_wr, zizhan_cam_capture::mmio_pause, 2000};
    if (hwPos == 2) {
      /* IMX596: 0x0202/0x0203 = lines, 0x0204/0x0205 = 1024 - (1024*256 / gain_q8) */
      const uint32_t ag = (ae.gain_q8 > 256u) ? (1024u - (262144u / ae.gain_q8)) : 0u;
      const uint16_t ag_clamped = (uint16_t)(ag > 960u ? 960u : ag);
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x0104u, 1u);
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x0202u, uint8_t(ae.exposure_lines >> 8));
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x0203u, uint8_t(ae.exposure_lines & 0xffu));
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x0204u, uint8_t(ag_clamped >> 8));
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x0205u, uint8_t(ag_clamped & 0xffu));
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x0104u, 0u);
    } else {
      /* OV13B10: 0x3501/0x3502 = lines, 0x3508 = gain_q8 >> 8, 0x3509 = gain_q8 & 0xfe */
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x3208u, 0x00u);
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x3501u, uint8_t(ae.exposure_lines >> 8));
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x3502u, uint8_t(ae.exposure_lines & 0xffu));
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x3508u, uint8_t(ae.gain_q8 >> 8));
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x3509u, uint8_t(ae.gain_q8 & 0xfeu));
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x3208u, 0x10u);
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x3208u, 0xa0u);
    }
  }

  bool captureOneFrame(uint32_t slotIdx, bool stampQr) {
    ZizhanCamCcOps csidOps = {&csid, zizhan_cam_capture::mmio_rd, zizhan_cam_capture::mmio_wr, zizhan_cam_capture::mmio_pause, 2000};
    ZizhanCamCcOps vfeOps  = {&vfe,  zizhan_cam_capture::mmio_rd, zizhan_cam_capture::mmio_wr, zizhan_cam_capture::mmio_pause, 2000};

    const uint32_t dstIova = (slotIdx & 1u) ? kOvSlot1Iova : kOvSlot0Iova;
    volatile uint8_t *rawVa = carveout.va + (dstIova - kCamCarveoutPa);

    /* Frame 0 may already have completed during openHardware's sensor check; for subsequent frames
     * (or if Frame 0 is still in flight), re-arm VFE-lite0 WM0 + CSID RDI0 RUP/AUP cleanly. */
    bool done = (slotIdx == 0u) && ((csid.r(0x8cu) & 0x6u) != 0u);
    if (!done) {
      if (slotIdx > 0u) {
        zizhan_vfe_lite_wm0_stop(&vfeOps);
        zizhan_vfe_lite_wm0_start(&vfeOps, dstIova, rawH, rawStride, addrShifted);
        csid.w(0x94u, 0xffffffffu);
        csid.w(0x14u, 1u);
        csid.w(0x018u, (1u << 4) | (1u << 20));
        csid.w(0x504u, 1u);
      }
      for (unsigned ms = 0; ms < 120; ++ms) {
        IOSleep(1);
        const uint32_t bd = csid.r(0x8cu);
        if (bd & 0x6u) {
          done = true;
          break;
        }
      }
    }
    if (!done) {
      /* Reset CSID RDI0 + WM0 cleanly on rare MIPI frame boundary miss */
      zizhan_csid_lite_stop_rdi0(&csidOps);
      zizhan_vfe_lite_wm0_stop(&vfeOps);
      (void)zizhan_csid_lite_reset(&csidOps);
      zizhan_vfe_lite_wm0_start(&vfeOps, dstIova, rawH, rawStride, addrShifted);
      zizhan_csid_lite_start_rdi0(&csidOps, csiphyId, 4u, 0x3210u, 0);
      return false;
    }

    const uint32_t nextPub = (__atomic_load_n(&gPublishedIdx, __ATOMIC_RELAXED) ^ 1u) & 1u;
    uint8_t *outBuf = gPublishedBuf[nextPub];
    if (!outBuf) return false;

    uint8_t *bgra = outBuf + sizeof(ZizhanCamFrameHeader);
    uint16_t mean10 = 0, r10 = 0, g10 = 0, b10 = 0;
    const int mirrorX = (reqPos == 2) ? 1 : 0; /* mirror front selfie naturally; rear stays true-to-world for QR! */
    const int flipY   = (hwPos == 1) ? 1 : 0;  /* OV13B10 roll=90/yaw=180 orientation adjustment */

    const uint16_t meanLuma = zizhan_cam_isp_raw10_to_bgra(
        const_cast<const uint8_t *>(rawVa), rawW, rawH, rawStride,
        bayerOrder, mirrorX, flipY,
        ae.black_level, awbR, awbB,
        bgra, ZIZHAN_CAM_OUT_W, ZIZHAN_CAM_OUT_H,
        &mean10, &r10, &g10, &b10);

    uint16_t flags = ZIZHAN_CAM_FLAG_OPTICAL;
    if (reqPos == 2 && hwPos == 1) flags |= ZIZHAN_CAM_FLAG_FALLBACK;
    if (stampQr) {
      zizhan_cam_stamp_test_qr(bgra, ZIZHAN_CAM_OUT_W, ZIZHAN_CAM_OUT_H);
      flags |= ZIZHAN_CAM_FLAG_QR_STAMP;
    }

    /* Closed-loop Gray-World AWB (damped 50%) + AE update every 3rd frame */
    uint16_t tgtR = 256u, tgtB = 256u;
    zizhan_cam_awb_gains(r10, g10, b10, ae.black_level, &tgtR, &tgtB);
    awbR = uint16_t((uint32_t(awbR) * 3u + tgtR) >> 2);
    awbB = uint16_t((uint32_t(awbB) * 3u + tgtB) >> 2);
    if ((slotIdx % 3u) == 2u && zizhan_cam_ae_step(&ae, mean10)) {
      applySensorAe();
    }

    uint64_t tsNs = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &tsNs);
    const uint32_t seq = __atomic_add_fetch(&gPublishedSeq, 1u, __ATOMIC_ACQ_REL);

    ZizhanCamFrameHeader hdr{};
    hdr.magic          = ZIZHAN_CAM_MAGIC;
    hdr.version        = ZIZHAN_CAM_VERSION;
    hdr.sensor_pos     = uint16_t(reqPos);
    hdr.width          = ZIZHAN_CAM_OUT_W;
    hdr.height         = ZIZHAN_CAM_OUT_H;
    hdr.stride         = ZIZHAN_CAM_OUT_STRIDE;
    hdr.seq            = seq;
    hdr.exposure_lines = ae.exposure_lines;
    hdr.analog_gain_q8 = ae.gain_q8;
    hdr.awb_r_q8       = awbR;
    hdr.awb_b_q8       = awbB;
    hdr.mean_raw10     = mean10;
    hdr.mean_luma8     = meanLuma;
    hdr.flags          = flags;
    hdr.timestamp_ns   = tsNs;

    IOLockLock(gLock);
    memcpy(outBuf, &hdr, sizeof(hdr));
    __atomic_store_n(&gPublishedIdx, nextPub, __ATOMIC_RELEASE);
    IOLockUnlock(gLock);
    return true;
  }

  void teardown() {
    ZizhanCamCcOps ccOps   = {&camcc,  zizhan_cam_capture::mmio_rd, zizhan_cam_capture::mmio_wr, zizhan_cam_capture::mmio_pause, 2000};
    ZizhanCamCcOps phyOps  = {&csiphy, zizhan_cam_capture::mmio_rd, zizhan_cam_capture::mmio_wr, zizhan_cam_capture::mmio_pause, 2000};
    ZizhanCamCcOps csidOps = {&csid,   zizhan_cam_capture::mmio_rd, zizhan_cam_capture::mmio_wr, zizhan_cam_capture::mmio_pause, 2000};
    ZizhanCamCcOps vfeOps  = {&vfe,    zizhan_cam_capture::mmio_rd, zizhan_cam_capture::mmio_wr, zizhan_cam_capture::mmio_pause, 2000};
    ZizhanCciOps   cciOps  = {&cci0,   zizhan_cam_capture::mmio_rd, zizhan_cam_capture::mmio_wr, zizhan_cam_capture::mmio_pause, 2000};

    if (streaming && cci0.va) {
      (void)zizhan_cci_write16_u8_m(&cciOps, 1u, cciAddr, 0x0100u, 0x00u);
      IOSleep(2);
    }
    if (csid.va) zizhan_csid_lite_stop_rdi0(&csidOps);
    if (vfe.va) zizhan_vfe_lite_wm0_stop(&vfeOps);
    if (csiphy.va) zizhan_csiphy_dphy_disable(&phyOps);
    streaming = false;

    if (rst.va) zizhan_cam_capture::gpio_drive(rst, 0);
    if (mclk.va && s_mclk) mclk.w(0, s_mclk);
    if (pad0.va && s_pad0) pad0.w(0, s_pad0);
    if (pad1.va && s_pad1) pad1.w(0, s_pad1);

    if (cciOn && camcc.va) {
      (void)zizhan_camcc_branch(&ccOps, 0x13144u, 0);
      cciOn = false;
    }
    if (mclkOn && camcc.va) {
      (void)zizhan_camcc_branch(&ccOps, mclkCbcr, 0);
      mclkOn = false;
    }
    if (phy3ClksOn && camcc.va) {
      (void)camccCsiphy3(&ccOps, false);
      phy3ClksOn = false;
    }
    if (ispClksOn && camcc.va) {
      zizhan_camcc_isp_clocks_off(&ccOps);
      (void)zizhan_camcc_gdsc(&ccOps, ZizhanCamCcTitanGdscr, 0);
      ispClksOn = false;
    }
    if (havePmI2c) {
      const ZizhanPm8008Ops pmOps = {&pmCtx, zizhan_cam_capture::pm_rd, zizhan_cam_capture::pm_wr};
      if (vdigLdo) (void)zizhan_pm8008_enable(&pmOps, vdigLdo, 0);
      else if (vdig.va) zizhan_cam_capture::gpio_drive(vdig, 0);
      if (vana.va) zizhan_cam_capture::gpio_drive(vana, 0);
      (void)zizhan_pm8008_enable(&pmOps, 5u, 0);
      havePmI2c = false;
    }
    smmuCtx.teardown();

    if (pm_en.va) pm_en.w(0, s_pm);
    if (vana.va) vana.w(0, s_vana);
    if (vdig.va) vdig.w(0, s_vdig);
    if (rst.va) rst.w(0, s_rst);
    if (i2c0.va) i2c0.w(0, s_i2c0);
    if (i2c1.va) i2c1.w(0, s_i2c1);
    if (gccCam.va) {
      gccCam.w(0x020, s_gcc_xo);
      gccCam.w(0x018, s_gcc_sf);
      gccCam.w(0x010, s_gcc_hf);
      gccCam.w(0x004, s_gcc_ahb);
    }

    pm_en.close(); vana.close(); vdig.close(); mclk.close(); rst.close();
    pad0.close(); pad1.close(); i2c0.close(); i2c1.close();
    carveout.close(); vfe.close(); csid.close(); tpg1.close(); csiphy.close();
    cci0.close(); camcc.close(); se.close(); wrap.close(); vote.close(); gccCam.close();
  }
};

static void streamWorker(void *, wait_result_t) {
  thread_set_thread_name(current_thread(), "zizhan_cam");
  for (;;) {
    int mode = __atomic_load_n(&gDesiredMode, __ATOMIC_ACQUIRE);
    const uint64_t lease = __atomic_load_n(&gLeaseUntilTicks, __ATOMIC_ACQUIRE);
    if (mode <= 0 || readCntvct() >= lease) {
      __atomic_store_n(&gActiveSensor, 0, __ATOMIC_RELEASE);
      IOSleep(40);
      continue;
    }

    /* Allocate double-buffered 1.23 MiB BGRA output slots lazily on first stream */
    if (!gPublishedBuf[0]) gPublishedBuf[0] = static_cast<uint8_t *>(IOMallocData(ZIZHAN_CAM_TOTAL_BYTES));
    if (!gPublishedBuf[1]) gPublishedBuf[1] = static_cast<uint8_t *>(IOMallocData(ZIZHAN_CAM_TOTAL_BYTES));
    if (!gPublishedBuf[0] || !gPublishedBuf[1]) {
      IOSleep(200);
      continue;
    }

    const int wantPos = (mode == ZIZHAN_CAM_CMD_FRONT) ? 2 : 1;
    StreamSession sess;
    bool opened = sess.openHardware(wantPos);
    if (!opened && wantPos == 2) {
      /* Automatic resilient fallback to mirrored OV13B10 if front IMX596 didn't stream */
      sess.reqPos = 2;
      opened = sess.openHardware(1);
      sess.reqPos = 2;
    }
    if (!opened) {
      IOLog("ZIZHAN_CAM_STREAM: open failed want_pos=%d; cooling down 500ms\n", wantPos);
      IOSleep(500);
      continue;
    }

    __atomic_store_n(&gActiveSensor, sess.hwPos, __ATOMIC_RELEASE);
    uint32_t localLoops = 0, publishedFrames = 0;
    while (true) {
      const int curMode = __atomic_load_n(&gDesiredMode, __ATOMIC_ACQUIRE);
      const uint64_t curLease = __atomic_load_n(&gLeaseUntilTicks, __ATOMIC_ACQUIRE);
      const int curWantPos = (curMode == ZIZHAN_CAM_CMD_FRONT) ? 2 : 1;
      if (curMode <= 0 || readCntvct() >= curLease || curWantPos != wantPos) {
        break;
      }
      uint32_t qrBoot = 0;
      (void)PE_parse_boot_argn("zizhan_cam_qr_overlay", &qrBoot, sizeof(qrBoot));
      const bool stampQr = (curMode == ZIZHAN_CAM_CMD_BACK_QR) || (qrBoot == 1u && sess.reqPos == 1);
      if (sess.captureOneFrame(localLoops++, stampQr)) {
        ++publishedFrames;
        IOSleep(32); /* pace at ~18-22 fps to keep CPU & power cool */
      } else {
        IOSleep(10);
      }
    }

    sess.teardown();
    __atomic_store_n(&gActiveSensor, 0, __ATOMIC_RELEASE);
    IOLog("ZIZHAN_CAM_STREAM: CLOSED want_pos=%d frames=%u loops=%u rails_off=1\n",
          wantPos, publishedFrames, localLoops);
  }
}

static int camCtrl(int cmd) {
  if (cmd == ZIZHAN_CAM_CMD_QUERY) {
    const uint32_t seq = __atomic_load_n(&gPublishedSeq, __ATOMIC_ACQUIRE);
    const uint32_t act = uint32_t(__atomic_load_n(&gActiveSensor, __ATOMIC_ACQUIRE)) & 0xffu;
    return int(((seq & 0x00ffffffu) << 8) | act);
  }
  if (cmd < 0 || cmd > 3) return EINVAL;
  if (cmd == ZIZHAN_CAM_CMD_OFF) {
    __atomic_store_n(&gDesiredMode, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&gLeaseUntilTicks, 0ull, __ATOMIC_RELEASE);
    return 0;
  }
  const uint64_t leaseTicks = (readCntfrq() * 7ull) / 2ull; /* 3.5 second crash-safe lease */
  __atomic_store_n(&gLeaseUntilTicks, readCntvct() + leaseTicks, __ATOMIC_RELEASE);
  __atomic_store_n(&gDesiredMode, cmd, __ATOMIC_RELEASE);

  IOLockLock(gLock);
  if (!gThreadRunning) {
    thread_t th = THREAD_NULL;
    if (kernel_thread_start(streamWorker, nullptr, &th) == KERN_SUCCESS) {
      thread_deallocate(th);
      gThreadRunning = true;
    }
  }
  IOLockUnlock(gLock);
  return 0;
}

static size_t camFrame(void *dst, size_t max_bytes, size_t offset) {
  if (__atomic_load_n(&gPublishedSeq, __ATOMIC_ACQUIRE) == 0u) return 0u;
  if (!dst) return ZIZHAN_CAM_TOTAL_BYTES;
  if (offset >= ZIZHAN_CAM_TOTAL_BYTES) return 0u;

  size_t avail = ZIZHAN_CAM_TOTAL_BYTES - offset;
  if (avail > max_bytes) avail = max_bytes;

  IOLockLock(gLock);
  const uint32_t idx = __atomic_load_n(&gPublishedIdx, __ATOMIC_ACQUIRE) & 1u;
  const uint8_t *src = gPublishedBuf[idx];
  if (src) {
    memcpy(dst, src + offset, avail);
  } else {
    avail = 0u;
  }
  IOLockUnlock(gLock);
  return avail;
}

static void start() {
  uint32_t off = 0;
  if (PE_parse_boot_argn("zizhan_cam_stream", &off, sizeof(off)) && off == 0u) return;
  if (!gLock) gLock = IOLockAlloc();
  if (!gLock) return;
  zizhan_cam_register(camCtrl, camFrame);
  IOLog("ZIZHAN_CAM_STREAM: registered on-demand sysctl service (idle=0mW)\n");
}

} // namespace zizhan_cam_stream

extern "C" int zizhan_cam_sleep_busy(void) {
  return (__atomic_load_n(&zizhan_cam_stream::gActiveSensor, __ATOMIC_ACQUIRE) != 0) ? 1 : 0;
}
