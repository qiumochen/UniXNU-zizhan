/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <pexpert/pexpert.h>
#include <string.h>
#include "QcomRpmh.h"
#include "ZizhanMicCodec.h"

extern "C" bool pong_rpmh_vote(uint32_t address, uint32_t value);

namespace {
constexpr uint64_t kCmdDbPhys = 0x80860000ULL;
constexpr size_t kCmdDbSize = 0x20000u;

struct MmioWin {
  IOMemoryMap *map = nullptr;
  volatile uint8_t *va = nullptr;
  bool open(uint64_t pa, size_t size, IOOptionBits extra = 0) {
    close();
    auto mem = IODeviceMemory::withRange(pa, size);
    if (!mem) {
      return false;
    }
    map = mem->map(kIOMapAnywhere | kIOMapInhibitCache | extra);
    mem->release();
    va = map ? reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress()) : nullptr;
    return va != nullptr;
  }
  void close() {
    if (map) {
      map->release();
      map = nullptr;
    }
    va = nullptr;
  }
  uint32_t rd32(uint32_t off) const {
    const uint32_t v = *reinterpret_cast<volatile uint32_t *>(va + off);
#if defined(__aarch64__) && defined(KERNEL)
    __asm__ volatile("dmb oshld" ::: "memory");
#endif
    return v;
  }
  void wr32(uint32_t off, uint32_t v) const {
    *reinterpret_cast<volatile uint32_t *>(va + off) = v;
#if defined(__aarch64__) && defined(KERNEL)
    __asm__ volatile("dsb oshst" ::: "memory");
#endif
  }
};

struct RuntimeCtx {
  MmioWin tlmm;
  MmioWin lpi;
  MmioWin slew;
  MmioWin mux;
  MmioWin va;
  MmioWin tx;
  MmioWin swrm;
  ZizhanMicHwState hw = {};
  uint32_t smpb10_addr = 0;
  uint32_t bobc1_addr = 0;
  bool bobc1_voted = false;
  bool active = false;

  void closeAll() {
    swrm.close();
    tx.close();
    va.close();
    mux.close();
    slew.close();
    lpi.close();
    tlmm.close();
  }
};

RuntimeCtx gMic;

uint32_t cb_tlmm_rd(void *c, uint32_t o) { return static_cast<RuntimeCtx *>(c)->tlmm.rd32(o); }
void cb_tlmm_wr(void *c, uint32_t o, uint32_t v) { static_cast<RuntimeCtx *>(c)->tlmm.wr32(o, v); }
uint32_t cb_lpi_rd(void *c, uint32_t o) { return static_cast<RuntimeCtx *>(c)->lpi.rd32(o); }
void cb_lpi_wr(void *c, uint32_t o, uint32_t v) { static_cast<RuntimeCtx *>(c)->lpi.wr32(o, v); }
uint32_t cb_slew_rd(void *c, uint32_t o) { return static_cast<RuntimeCtx *>(c)->slew.rd32(o); }
void cb_slew_wr(void *c, uint32_t o, uint32_t v) { static_cast<RuntimeCtx *>(c)->slew.wr32(o, v); }
uint32_t cb_mux_rd(void *c, uint32_t o) { return static_cast<RuntimeCtx *>(c)->mux.rd32(o); }
void cb_mux_wr(void *c, uint32_t o, uint32_t v) { static_cast<RuntimeCtx *>(c)->mux.wr32(o, v); }
uint32_t cb_va_rd(void *c, uint32_t o) { return static_cast<RuntimeCtx *>(c)->va.rd32(o); }
void cb_va_wr(void *c, uint32_t o, uint32_t v) { static_cast<RuntimeCtx *>(c)->va.wr32(o, v); }
uint32_t cb_tx_rd(void *c, uint32_t o) { return static_cast<RuntimeCtx *>(c)->tx.rd32(o); }
void cb_tx_wr(void *c, uint32_t o, uint32_t v) { static_cast<RuntimeCtx *>(c)->tx.wr32(o, v); }
uint32_t cb_swrm_rd(void *c, uint32_t o) { return static_cast<RuntimeCtx *>(c)->swrm.rd32(o); }
void cb_swrm_wr(void *c, uint32_t o, uint32_t v) { static_cast<RuntimeCtx *>(c)->swrm.wr32(o, v); }
void cb_delay_us(void *, unsigned us) { IODelay(us); }
void cb_sleep_ms(void *, unsigned ms) { IOSleep(ms); }

const ZizhanMicHwOps kOps = {
    &gMic,
    cb_tlmm_rd, cb_tlmm_wr,
    cb_lpi_rd,  cb_lpi_wr,
    cb_slew_rd, cb_slew_wr,
    cb_mux_rd,  cb_mux_wr,
    cb_va_rd,   cb_va_wr,
    cb_tx_rd,   cb_tx_wr,
    cb_swrm_rd, cb_swrm_wr,
    cb_delay_us,
    cb_sleep_ms,
};

bool voteRails() {
  uint8_t *db = static_cast<uint8_t *>(IOMallocData(kCmdDbSize));
  if (!db) {
    return false;
  }
  MmioWin dbw;
  if (!dbw.open(kCmdDbPhys, kCmdDbSize, kIOMapReadOnly)) {
    IOFreeData(db, kCmdDbSize);
    return false;
  }
  for (size_t off = 0; off < kCmdDbSize; off += 4u) {
    const uint32_t w = dbw.rd32((uint32_t)off);
    memcpy(db + off, &w, 4u);
  }
  dbw.close();
  QcomRpmhResource smpb10 = {}, bobc1 = {};
  const bool have_s10 = (qcom_cmd_db_find(db, kCmdDbSize, "smpb10", &smpb10) == QcomRpmhOk && smpb10.address != 0u);
  const bool have_bob = (qcom_cmd_db_find(db, kCmdDbSize, "bobc1", &bobc1) == QcomRpmhOk && bobc1.address != 0u);
  IOFreeData(db, kCmdDbSize);
  if (!have_s10 || !have_bob) {
    IOLog("ZIZHAN_MIC_CODEC: FAIL cmd-db smpb10=%x bobc1=%x\n", smpb10.address, bobc1.address);
    return false;
  }
  gMic.smpb10_addr = smpb10.address;
  gMic.bobc1_addr = bobc1.address;
  const bool s10_ok = pong_rpmh_vote(smpb10.address + 8u, 7u) &&
                      pong_rpmh_vote(smpb10.address + 0u, 1800u) &&
                      pong_rpmh_vote(smpb10.address + 4u, 1u);
  const bool bob_ok = pong_rpmh_vote(bobc1.address + 8u, 7u) &&
                      pong_rpmh_vote(bobc1.address + 0u, 3296u) &&
                      pong_rpmh_vote(bobc1.address + 4u, 1u);
  gMic.bobc1_voted = bob_ok;
  IOLog("ZIZHAN_MIC_CODEC: rails smpb10=%x:%u bobc1=%x:%u\n",
        smpb10.address, s10_ok ? 1u : 0u, bobc1.address, bob_ok ? 1u : 0u);
  return s10_ok && bob_ok;
}
}  // namespace

extern "C" int zizhan_mic_codec_start(uint32_t *channel_mask_out) {
  if (channel_mask_out) {
    *channel_mask_out = 0u;
  }
  if (gMic.active) {
    zizhan_mic_codec_stop();
  }
  if (!voteRails()) {
    return 0;
  }
  if (!gMic.tlmm.open(ZIZHAN_MIC_TLMM_BASE + ZIZHAN_MIC_WCD_RESET_PIN * 0x1000ULL, 0x1000u) ||
      !gMic.lpi.open(ZIZHAN_MIC_LPASS_LPI_TLMM, 0x10000u) ||
      !gMic.slew.open(ZIZHAN_MIC_LPASS_SLEW, 0x1000u) ||
      !gMic.mux.open(ZIZHAN_MIC_VA_ISLAND_MUX, 0x1000u) ||
      !gMic.va.open(ZIZHAN_MIC_VA_MACRO, 0x1000u) ||
      !gMic.tx.open(ZIZHAN_MIC_TX_MACRO, 0x1000u) ||
      !gMic.swrm.open(ZIZHAN_MIC_VA_SWRM, 0x2000u)) {
    IOLog("ZIZHAN_MIC_CODEC: FAIL mmio-map\n");
    if (gMic.bobc1_voted && gMic.bobc1_addr) {
      (void)pong_rpmh_vote(gMic.bobc1_addr + 4u, 0u);
      gMic.bobc1_voted = false;
    }
    gMic.closeAll();
    return 0;
  }
  unsigned dec_arg = 0u;
  if (PE_parse_boot_argn("zizhan_mic_dec", &dec_arg, sizeof(dec_arg)) && dec_arg > 7u) {
    dec_arg = 0u;
  }
  unsigned gain_arg = 12u;
  if (PE_parse_boot_argn("zizhan_mic_gain", &gain_arg, sizeof(gain_arg)) && gain_arg > 20u) {
    gain_arg = 12u;
  }
  const int ok = zizhan_mic_hw_bringup(&kOps, &gMic.hw, (uint8_t)dec_arg, (uint8_t)gain_arg);
  IOLog("ZIZHAN_MIC_CODEC: %s stage=%d swrm_ver=%x clk=%x slaves=%x dev=%u id=%x:%x devid=%x efuse=%x bank=%u dec=%u mask=%x gain=%u timeouts=%u audio_output=0\n",
        ok ? "PASS" : "FAIL", gMic.hw.stage, gMic.hw.swrm_ver, gMic.hw.clk_status, gMic.hw.slaves,
        gMic.hw.slave_dev, gMic.hw.slave_id1, gMic.hw.slave_id0, gMic.hw.dev_id, gMic.hw.efuse,
        gMic.hw.bank_after, gMic.hw.dec_idx, gMic.hw.channel_mask, gMic.hw.adc_gain, gMic.hw.fifo_timeouts);
  if (!ok) {
    if (gMic.bobc1_voted && gMic.bobc1_addr) {
      (void)pong_rpmh_vote(gMic.bobc1_addr + 4u, 0u);
      gMic.bobc1_voted = false;
    }
    gMic.closeAll();
    return 0;
  }
  gMic.active = true;
  if (channel_mask_out) {
    *channel_mask_out = gMic.hw.channel_mask;
  }
  return 1;
}

extern "C" void zizhan_mic_codec_unmute(void) {
  if (!gMic.active) {
    return;
  }
  zizhan_mic_hw_unmute(&kOps, &gMic.hw);
  const uint32_t ctl_off = 0x0400u + 0x80u * gMic.hw.dec_idx;
  const uint32_t sec6_off = 0x0418u + 0x80u * gMic.hw.dec_idx;
  const uint16_t bank_off = (uint16_t)(0x10u * (gMic.swrm.rd32(0x104cu) & 1u));
  uint8_t dp1_prep = 0u, dp1_ch = 0u, dp1_lane = 0u, dp1_hctrl = 0u, adc1_ctl = 0u, chmap = 0u;
  (void)zizhan_swrm_read(&kOps, &gMic.hw, gMic.hw.slave_dev, 0x0104u, &dp1_prep);
  (void)zizhan_swrm_read(&kOps, &gMic.hw, gMic.hw.slave_dev, (uint16_t)(0x0120u + bank_off), &dp1_ch);
  (void)zizhan_swrm_read(&kOps, &gMic.hw, gMic.hw.slave_dev, (uint16_t)(0x0128u + bank_off), &dp1_lane);
  (void)zizhan_swrm_read(&kOps, &gMic.hw, gMic.hw.slave_dev, (uint16_t)(0x0126u + bank_off), &dp1_hctrl);
  (void)zizhan_swrm_read(&kOps, &gMic.hw, gMic.hw.slave_dev, 0x300eu, &adc1_ctl);
  (void)zizhan_swrm_read(&kOps, &gMic.hw, gMic.hw.slave_dev, 0x3405u, &chmap);
  IOLog("ZIZHAN_MIC_CODEC: UNMUTE dec=%u tx_ctl=%x tx_sec6=%x tx_88=%x va_c4=%x va_dc=%x swrm_bank=%x dp1_prep=%x dp1_ch=%x dp1_lane=%x dp1_hctrl=%x adc1=%x chmap=%x audio_output=0\n",
        gMic.hw.dec_idx, gMic.tx.rd32(ctl_off), gMic.tx.rd32(sec6_off), gMic.tx.rd32(0x0088u),
        gMic.va.rd32(0x00c4u), gMic.va.rd32(0x00dcu), gMic.swrm.rd32(0x104cu),
        dp1_prep, dp1_ch, dp1_lane, dp1_hctrl, adc1_ctl, chmap);
}

extern "C" void zizhan_mic_codec_stop(void) {
  if (!gMic.active) {
    return;
  }
  zizhan_mic_hw_teardown(&kOps, &gMic.hw);
  bool bob_rel = true;
  if (gMic.bobc1_voted && gMic.bobc1_addr) {
    bob_rel = pong_rpmh_vote(gMic.bobc1_addr + 4u, 0u);
    gMic.bobc1_voted = false;
  }
  gMic.closeAll();
  gMic.active = false;
  IOLog("ZIZHAN_MIC_CODEC: STOP teardown_ok=%u bob_released=%u timeouts=%u audio_output=0\n",
        gMic.hw.teardown_ok, bob_rel ? 1u : 0u, gMic.hw.fifo_timeouts);
}
