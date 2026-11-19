/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* BT-01 step 1 (zizhan_bt_probe=1): read-only inventory of the QCA6490 Bluetooth UART path.
 * Android live DT: HS UART qupv3_se20 @0x894000 (wrap2 S5), pins GPIO76 CTS / 77 RTS / 78 TX / 79 RX
 * (function qup20 = 1), BT_EN GPIO81, SW_CTRL GPIO82, XO clock GPIO204. GCC (Linux gcc-sm8450.c):
 * wrap2 vote register 0x162010 (bit0 core, 1 S AHB, 2 M AHB, 3 core2x, 9 S5), S5 branch 0x12e610,
 * S5 RCG 0x12e618 (CMD, CFG, M, N, D), M/S AHB branches 0x12e004/0x12e008, core 0x133280/0x13328c.
 * Nothing is written. The SE is only read when its S5 and both AHB branches report running. */
#pragma once
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <string.h>
#include <kern/clock.h>
#include <mach/mach_time.h>

extern "C" struct vnode *rootvnode;

namespace zizhan_bt_probe {
static uint32_t probe_mode;
struct Window {
  IOMemoryMap *map = nullptr;
  volatile uint8_t *va = nullptr;
  bool writable = false;
  bool open(uint64_t base, bool write = false) {
    writable = write;
    auto memory = IODeviceMemory::withRange(base, 0x1000);
    map = memory ? memory->map(kIOMapAnywhere | kIOMapInhibitCache | (write ? 0 : kIOMapReadOnly)) : nullptr;
    if (memory) memory->release();
    va = map ? reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress()) : nullptr;
    return va != nullptr;
  }
  uint32_t read(uint32_t at) const {
    const uint32_t v = *reinterpret_cast<const volatile uint32_t *>(va + at);
    __asm__ volatile("dmb oshld" ::: "memory");
    return v;
  }
  void write(uint32_t at, uint32_t v) {
    if (!writable) return;
    __asm__ volatile("dmb oshst" ::: "memory");
    *reinterpret_cast<volatile uint32_t *>(va + at) = v;
    __asm__ volatile("dsb sy" ::: "memory");
  }
  ~Window() { if (map) map->release(); }
};


/* BT-01 step 2 (zizhan_bt_probe=2): first HCI handshake, no firmware download, no radio.
 * Clocks follow Linux gcc-sm8450.c (vote only ever adds bits, like SE15), the SE is set up like
 * qcom_geni_serial.c in FIFO mode at 115200 8N1 with CTS flow control. Every wait is bounded. */
static uint64_t now_us() { uint64_t ns = 0; absolutetime_to_nanoseconds(mach_absolute_time(), &ns); return ns / 1000; }
static unsigned rx_bytes(Window &se, uint8_t *out, unsigned cap, unsigned wait_ms) {
  unsigned n = 0;
  const uint64_t end = now_us() + uint64_t(wait_ms) * 1000;
  while (now_us() < end && n < cap) {
    const uint32_t st = se.read(0x804);
    const uint32_t words = st & 0x1ffffff;
    if (!words) { IOSleep(2); continue; }
    for (uint32_t w = 0; w < words && n < cap; ++w) {
      const uint32_t v = se.read(0x780);
      unsigned valid = 4;
      if (w + 1 == words && (st & 0x80000000u)) { valid = (st >> 28) & 7; if (!valid) valid = 4; }
      for (unsigned b = 0; b < valid && n < cap; ++b) out[n++] = uint8_t(v >> (8 * b));
    }
  }
  return n;
}
static bool tx_bytes(Window &se, const uint8_t *data, unsigned len) {
  se.write(0x618, 0xffffffff);                    // M_IRQ_CLEAR
  se.write(0x270, len);                           // UART_TX_TRANS_LEN
  se.write(0x600, 1u << 27);                      // M_CMD0 = UART_START_TX
  for (unsigned i = 0; i < len; i += 4) {
    uint32_t w = 0;
    for (unsigned b = 0; b < 4 && i + b < len; ++b) w |= uint32_t(data[i + b]) << (8 * b);
    const uint64_t room = now_us() + 100000;
    while ((se.read(0x800) & 0x0fffffffu) >= 14 && now_us() < room) IODelay(20);   // TX_FIFO_STATUS word count
    se.write(0x700, w);                           // TX_FIFOn
  }
  const uint64_t end = now_us() + 200000;
  while (now_us() < end) {
    if (se.read(0x610) & 1u) { se.write(0x618, 1u); return true; }   // M_CMD_DONE
    IOSleep(1);
  }
  return false;
}
static void log_bytes(const char *what, const uint8_t *b, unsigned n) {
  char text[3 * 40 + 1] = {};
  unsigned k = 0;
  for (unsigned i = 0; i < n && i < 40; ++i) k += snprintf(text + k, sizeof text - k, "%02x ", b[i]);
  IOLog("ZIZHAN_BT_PROBE: %s bytes=%u [%s]\n", what, n, text);
}

/* HCI event reader: keeps bytes across calls, returns one complete H4 event (0x04 code len payload). */
static uint8_t ev_buf[512];
static unsigned ev_have;
static unsigned read_event(Window &se, uint8_t *out, unsigned cap, unsigned wait_ms) {
  const uint64_t end = now_us() + uint64_t(wait_ms) * 1000;
  for (;;) {
    while (ev_have && ev_buf[0] != 0x04) { memmove(ev_buf, ev_buf + 1, --ev_have); }
    if (ev_have >= 3 && ev_have >= 3u + ev_buf[2]) {
      const unsigned n = 3u + ev_buf[2];
      const unsigned copy = n < cap ? n : cap;
      memcpy(out, ev_buf, copy);
      memmove(ev_buf, ev_buf + n, ev_have - n); ev_have -= n;
      return copy;
    }
    if (now_us() >= end) return 0;
    const unsigned got = rx_bytes(se, ev_buf + ev_have, sizeof ev_buf - ev_have, 5);
    ev_have += got;
    if (ev_have == sizeof ev_buf) ev_have = 0;     // garbage overflow: drop
  }
}
static bool chosen_blob(const char *name, uint8_t *&data, unsigned &size) {
  auto chosen = IORegistryEntry::fromPath("/chosen", gIODTPlane);
  auto blob = chosen ? OSDynamicCast(OSData, chosen->getProperty(name)) : nullptr;
  data = nullptr; size = 0;
  if (blob && blob->getLength() && blob->getLength() <= 256 * 1024) {
    size = blob->getLength();
    data = static_cast<uint8_t *>(IOMalloc(size));
    if (data) memcpy(data, blob->getBytesNoCopy(), size); else size = 0;
  }
  if (chosen) chosen->release();
  return data != nullptr;
}
/* Linux btqca qca_download_firmware(): 243-byte EDL_PATCH_TLV_REQ_CMD segments; with download mode 0
 * every segment is acked by Command Complete (rtype 0x1e on WCN3991+), otherwise only the last one. */
static bool download(Window &se, const char *what, const uint8_t *data, unsigned size, unsigned mode) {
  uint8_t cmd[4 + 2 + 243], ev[64];
  unsigned segments = 0, acks = 0;
  for (unsigned at = 0; at < size;) {
    const unsigned seg = size - at < 243 ? size - at : 243;
    const bool last = at + seg >= size || seg < 243;
    cmd[0] = 0x01; cmd[1] = 0x00; cmd[2] = 0xfc; cmd[3] = uint8_t(seg + 2); cmd[4] = 0x1e; cmd[5] = uint8_t(seg);
    memcpy(cmd + 6, data + at, seg);
    if (!tx_bytes(se, cmd, 6 + seg)) { IOLog("ZIZHAN_BT_PROBE: %s tx-timeout segment=%u\n", what, segments); return false; }
    ++segments; at += seg;
    if (mode == 0 || last) {
      const unsigned n = read_event(se, ev, sizeof ev, 2000);
      if (n < 7 || ev[1] != 0x0e || ev[4] != 0x00 || ev[5] != 0xfc || ev[6] != 0x00) {
        IOLog("ZIZHAN_BT_PROBE: %s bad-ack segment=%u bytes=%u code=%x status=%x\n", what, segments, n, n > 1 ? ev[1] : 0, n > 6 ? ev[6] : 0xff);
        log_bytes("bad-ack", ev, n);
        return false;
      }
      ++acks;
    }
  }
  IOLog("ZIZHAN_BT_PROBE: %s done bytes=%u segments=%u acks=%u mode=%u\n", what, size, segments, acks, mode);
  return true;
}
/* NVM for this polled host: 115200 (QCA_BAUDRATE_115200 = 0), no in-band sleep, no deep sleep. */
static void nvm_fixup(uint8_t *d, unsigned size) {
  if (size < 4) return;
  unsigned off = 0;
  uint32_t tl = d[0] | d[1] << 8 | d[2] << 16 | uint32_t(d[3]) << 24;
  if ((tl & 0xff) == 4 && size >= 8) { off = 4; tl = d[4] | d[5] << 8 | d[6] << 16 | uint32_t(d[7]) << 24; }
  if ((tl & 0xff) != 2) { IOLog("ZIZHAN_BT_PROBE: nvm type=%u untouched\n", tl & 0xff); return; }
  const unsigned length = tl >> 8, base = off + 4;
  unsigned idx = 0, hci = 0, sleep = 0;
  while (idx + 12 <= length && base + idx + 12 <= size) {
    uint8_t *t = d + base + idx;
    const unsigned id = t[0] | t[1] << 8, len = t[2] | t[3] << 8;
    if (base + idx + 12 + len > size) break;
    if (id == 17 && len >= 3) { t[12] &= ~0x80u; t[13] = 0; ++hci; }
    if (id == 27 && len >= 1) { t[12] &= ~0x01u; ++sleep; }
    idx += 12 + len;
  }
  IOLog("ZIZHAN_BT_PROBE: nvm fixup hci=%u deep_sleep=%u baud=115200 ibs=0\n", hci, sleep);
}

static void active() {
  Window vote, wrap, wrapper, se, pins[6];
  static const uint16_t pin_no[6] = {76, 77, 78, 79, 81, 82};
  if (!vote.open(0x162000, true) || !wrap.open(0x12e000, true) || !wrapper.open(0x8c0000)) {
    IOLog("ZIZHAN_BT_PROBE: FAIL gcc-map\n");
    return;
  }
  for (unsigned i = 0; i < 6; ++i)
    if (!pins[i].open(0xf100000ull + uint64_t(pin_no[i]) * 0x1000, true)) { IOLog("ZIZHAN_BT_PROBE: FAIL pin-map %u\n", pin_no[i]); return; }
  uint32_t saved_cfg[6], saved_io[6];
  for (unsigned i = 0; i < 6; ++i) { saved_cfg[i] = pins[i].read(0); saved_io[i] = pins[i].read(4); }
  if ((saved_cfg[4] & 0x3c) || (saved_io[4] & 2)) { IOLog("ZIZHAN_BT_PROBE: SKIP bt_en-not-idle cfg=%x io=%x\n", saved_cfg[4], saved_io[4]); return; }
  // 1. Clock rate. QUP RCGs run in DFS mode (bootloader table, the SE picks a level with SE_GENI_CLK_SEL):
  // never reprogram the RCG then; pick the table level that divides 115200 x oversampling exactly.
  // Without DFS, program 7.3728 MHz like Linux (GPLL0_OUT_EVEN, M 384 / N 15625, dual edge).
  const uint32_t dfsr = wrap.read(0x62c);
  int level = -1;
  uint32_t level_rate = 0;
  bool updated = false;
  const uint32_t hw_early = wrapper.read(0x4);
  const unsigned sampling_early = hw_early >= 0x20050000u ? 16u : 32u;
  if (dfsr & 1u) {
    for (unsigned l = 0; l < 8; ++l) {
      const uint32_t perf = wrap.read(0x634 + 4 * l), mreg = wrap.read(0x674 + 4 * l), nreg = wrap.read(0x6b4 + 4 * l);
      const uint32_t src = (perf >> 8) & 7, pre = perf & 31, mode = (perf >> 12) & 3;
      uint64_t parent = src == 0 ? 19200000ull : src == 1 ? 600000000ull : src == 6 ? 300000000ull : 0;
      uint64_t rate = pre ? parent * 2 / (pre + 1) : parent;
      if (mode == 2 && mreg) { const uint32_t n = ((~nreg) & 0xffff) + mreg; rate = n ? rate * mreg / n : 0; }
      IOLog("ZIZHAN_BT_PROBE: dfs level=%u perf=%x m=%x n=%x rate=%llu\n", l, perf, mreg, nreg, (unsigned long long)rate);
      const uint64_t unit = 115200ull * sampling_early;
      if (rate && rate % unit == 0 && rate / unit <= 4095 && (level < 0 || rate < level_rate)) { level = int(l); level_rate = uint32_t(rate); }
    }
  } else {
    wrap.write(0x620, 384);
    wrap.write(0x624, ~uint32_t(15625 - 384) & 0xffff);
    wrap.write(0x628, ~uint32_t(15625) & 0xffff);
    wrap.write(0x61c, (wrap.read(0x61c) & ~0x1037ffu) | 1u | (6u << 8) | (2u << 12));
    wrap.write(0x618, wrap.read(0x618) | 1u);       // CMD_UPDATE
    for (unsigned i = 0; i < 500 && !updated; ++i) { updated = !(wrap.read(0x618) & 1u); if (!updated) IODelay(10); }
    level = 0; level_rate = 7372800;
  }
  IOLog("ZIZHAN_BT_PROBE: rate dfs=%u level=%d rate=%u rcg_written=%u\n", dfsr & 1u, level, level_rate, (dfsr & 1u) ? 0u : 1u);
  if (level < 0) { IOLog("ZIZHAN_BT_PROBE: SKIP no-exact-uart-rate writes_gcc=0\n"); return; }
  // 2. Branch votes: wrap2 core, S AHB, M AHB, core2x, S5 (add only).
  const uint32_t before = vote.read(0x10);
  vote.write(0x10, before | 0x20fu);
  bool running = false;
  for (unsigned i = 0; i < 1000 && !running; ++i) {
    // AHB branches with hardware clock gating (bit 1) may keep HALT set while idle (same rule as SE15).
    const uint32_t m_ahb = wrap.read(0x004), s_ahb = wrap.read(0x008);
    running = !(wrap.read(0x610) & 0x80000000u) && (!(m_ahb & 0x80000000u) || (m_ahb & 2u)) && (!(s_ahb & 0x80000000u) || (s_ahb & 2u));
    if (!running) IODelay(100);
  }
  IOLog("ZIZHAN_BT_PROBE: clock rcg_updated=%u cfg=%x m=%x n=%x d=%x vote=%x->%x s5=%x m_ahb=%x s_ahb=%x running=%u\n",
        updated ? 1u : 0u, wrap.read(0x61c), wrap.read(0x620), wrap.read(0x624), wrap.read(0x628), before, vote.read(0x10),
        wrap.read(0x610), wrap.read(0x004), wrap.read(0x008), running ? 1u : 0u);
  if (!running) { IOLog("ZIZHAN_BT_PROBE: FAIL clock-not-running se_access=0\n"); return; }
  if (!se.open(0x894000, true)) { IOLog("ZIZHAN_BT_PROBE: FAIL se-map\n"); return; }
  const uint32_t fw = se.read(0x68), hw = wrapper.read(0x4);
  IOLog("ZIZHAN_BT_PROBE: se20 fw=%x protocol=%u qup_hw=%x if_disable=%x status=%x\n", fw, (fw >> 8) & 255, hw, se.read(0x64), se.read(0x40));
  if (((fw >> 8) & 255) != 2 || (se.read(0x64) & 1)) { IOLog("ZIZHAN_BT_PROBE: SKIP not-uart-firmware writes_se=0\n"); return; }
  // 3. Pins: UART function 1, 2 mA; CTS no pull, RTS pull-down, TX pull-up, RX pull-up.
  static const uint32_t pull[4] = {0, 1, 3, 3};
  for (unsigned i = 0; i < 4; ++i) pins[i].write(0, (saved_cfg[i] & ~0x3ffu) | (1u << 2) | pull[i]);
  // 4. GENI FIFO-mode UART (qcom_geni_serial port_setup + set_termios).
  se.write(0x28, se.read(0x28) | 0x7f);           // SE_GENI_CGC_CTRL
  se.write(0xe30, se.read(0xe30) | 0xf);          // SE_DMA_GENERAL_CFG
  se.write(0x24, 0x7f);                           // GENI_OUTPUT_CTRL
  se.write(0x20, 1);                              // GENI_FORCE_DEFAULT_REG
  se.write(0xe18, 0);                             // SE_GSI_EVENT_EN
  se.write(0x618, 0xffffffff); se.write(0x648, 0xffffffff);
  se.write(0x258, se.read(0x258) & ~1u);          // FIFO mode
  se.write(0x260, 0x4380e); se.write(0x264, 0xc3e0e);   // TX packing 8 bit x 4
  se.write(0x284, 0x4380e); se.write(0x288, 0xc3e0e);   // RX packing
  se.write(0x254, 0);                             // BYTE_GRAN
  se.write(0x810, 2); se.write(0x814, 14);        // RX watermark / RFR watermark
  se.write(0x294, 160);                           // RX stale count
  const unsigned sampling = hw >= 0x20050000u ? 16u : 32u;
  const unsigned div = (level_rate + 115200u * sampling - 1) / (115200u * sampling);
  se.write(0x7c, uint32_t(level));                // SE_GENI_CLK_SEL (DFS level)
  se.write(0x48, 1u | (div << 4)); se.write(0x4c, 1u | (div << 4));
  se.write(0x22c, 0);                             // loopback off
  se.write(0x25c, 0); se.write(0x2a4, 0);         // TX: no parity, CTS honoured
  se.write(0x280, 0); se.write(0x2a8, 0);
  se.write(0x268, 8); se.write(0x28c, 8); se.write(0x26c, 0);
  se.write(0x630, 1u << 27);                      // S_CMD0 = UART_START_READ
  IOLog("ZIZHAN_BT_PROBE: uart sampling=%u div=%u m_clk=%x s_status=%x ios=%x\n", sampling, div, se.read(0x48), se.read(0x40), se.read(0x908));
  // 5. BT_EN high, let the chip boot its ROM.
  pins[4].write(0, (saved_cfg[4] & ~0x3ffu) | (1u << 9));
  pins[4].write(4, 2u);
  IOSleep(200);
  uint8_t rx[64];
  unsigned n = rx_bytes(se, rx, sizeof rx, 100);
  log_bytes("after-enable", rx, n);
  IOLog("ZIZHAN_BT_PROBE: bt_en=%u sw_ctrl=%u cts_in=%u ios=%x\n", (pins[4].read(4) >> 1) & 1, pins[5].read(4) & 1, pins[0].read(4) & 1, se.read(0x908));
  static const uint8_t reset[] = {0x01, 0x03, 0x0c, 0x00};
  bool sent = tx_bytes(se, reset, sizeof reset);
  n = rx_bytes(se, rx, sizeof rx, 500);
  IOLog("ZIZHAN_BT_PROBE: hci-reset sent=%u m_irq=%x\n", sent ? 1u : 0u, se.read(0x610));
  log_bytes("hci-reset-event", rx, n);
  static const uint8_t version[] = {0x01, 0x00, 0xfc, 0x01, 0x19};   // QCA EDL_PATCH_GETVER
  sent = tx_bytes(se, version, sizeof version);
  n = rx_bytes(se, rx, sizeof rx, 500);
  IOLog("ZIZHAN_BT_PROBE: getver sent=%u\n", sent ? 1u : 0u);
  log_bytes("getver-event", rx, n);
  ev_have = 0;
  static const uint8_t bid_cmd[] = {0x01, 0x00, 0xfc, 0x01, 0x23};   // EDL_GET_BID_REQ_CMD
  sent = tx_bytes(se, bid_cmd, sizeof bid_cmd);
  n = read_event(se, rx, sizeof rx, 500);
  log_bytes("board-id-event", rx, n);
  // Command Complete: 04 0e len 01 00 fc | cresp 00 | rtype 23 | data[0] data[1] data[2]; bid = data[1]<<8 | data[2]
  if (n >= 11 && rx[1] == 0x0e && rx[6] == 0x00 && rx[7] == 0x23)
    IOLog("ZIZHAN_BT_PROBE: board_id=%x\n", (unsigned(rx[9]) << 8) | rx[10]);
  if (probe_mode == 3) {
    uint8_t *patch = nullptr, *nvm = nullptr;
    unsigned patch_size = 0, nvm_size = 0;
    const bool have = chosen_blob("zizhan-bt-rampatch", patch, patch_size) && chosen_blob("zizhan-bt-nvm", nvm, nvm_size);
    IOLog("ZIZHAN_BT_PROBE: firmware chosen=%u rampatch=%u nvm=%u\n", have ? 1u : 0u, patch_size, nvm_size);
    if (have && patch_size > 16) {
      const unsigned mode = patch[14];              // tlv_type_patch.download_mode
      bool ok = download(se, "rampatch", patch, patch_size, mode);
      IOSleep(10);
      if (ok) { nvm_fixup(nvm, nvm_size); ok = download(se, "nvm", nvm, nvm_size, 0); }
      if (ok) {
        sent = tx_bytes(se, reset, sizeof reset);
        n = read_event(se, rx, sizeof rx, 1000);
        log_bytes("post-patch-reset", rx, n);
        static const uint8_t bdaddr[] = {0x01, 0x09, 0x10, 0x00};   // HCI Read BD_ADDR
        sent = tx_bytes(se, bdaddr, sizeof bdaddr);
        n = read_event(se, rx, sizeof rx, 500);
        if (n >= 13 && rx[1] == 0x0e && rx[6] == 0x00)
          IOLog("ZIZHAN_BT_PROBE: bd_addr status=0 oui=%02x:%02x:%02x (rest not logged)\n", rx[12], rx[11], rx[10]);
        else log_bytes("bd-addr-event", rx, n);
        static const uint8_t local_ver[] = {0x01, 0x01, 0x10, 0x00};  // HCI Read Local Version
        sent = tx_bytes(se, local_ver, sizeof local_ver);
        n = read_event(se, rx, sizeof rx, 500);
        log_bytes("local-version-event", rx, n);
      }
    }
    if (patch) IOFree(patch, patch_size);
    if (nvm) IOFree(nvm, nvm_size);
  }
  // 6. Cleanup: stop RX, BT_EN low, pins back. Clock votes stay (never removed, as SE15).
  se.write(0x634, 4u);                            // S_CMD_CTRL cancel
  IOSleep(5);
  pins[4].write(4, saved_io[4] & ~2u);
  pins[4].write(0, saved_cfg[4]);
  for (unsigned i = 0; i < 4; ++i) pins[i].write(0, saved_cfg[i]);
  IOLog("ZIZHAN_BT_PROBE: DONE active bt_en_restored=%u firmware_download=%u radio=0\n", (pins[4].read(4) >> 1) & 1 ? 0u : 1u, probe_mode == 3 ? 1u : 0u);
}

static void finish() {
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}

static void worker(void *, wait_result_t) {
  for (unsigned i = 0; i < 1200 && !__atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE); ++i) IOSleep(100);
  IOSleep(30000);  // after Wi-Fi bring-up, which shares the chip's power
  if (probe_mode >= 2) { active(); finish(); return; }
  {
    Window vote, wrap, core;
    if (!vote.open(0x162000) || !wrap.open(0x12e000) || !core.open(0x133000)) {
      IOLog("ZIZHAN_BT_PROBE: FAIL gcc-map\n");
      finish();
      return;
    }
    const uint32_t votes = vote.read(0x10), s5 = wrap.read(0x610), mahb = wrap.read(0x004), sahb = wrap.read(0x008);
    IOLog("ZIZHAN_BT_PROBE: gcc vote=%x s5_cbcr=%x m_ahb=%x s_ahb=%x core=%x core2x=%x\n", votes, s5, mahb, sahb,
          core.read(0x280), core.read(0x28c));
    IOLog("ZIZHAN_BT_PROBE: rcg cmd=%x cfg=%x m=%x n=%x d=%x\n", wrap.read(0x618), wrap.read(0x61c), wrap.read(0x620),
          wrap.read(0x624), wrap.read(0x628));
    static const uint16_t pins[] = {76, 77, 78, 79, 80, 81, 82, 204};
    for (uint16_t pin : pins) {
      Window p;
      if (!p.open(0xf100000ull + uint64_t(pin) * 0x1000)) {
        IOLog("ZIZHAN_BT_PROBE: pin=%u map-fail\n", pin);
        continue;
      }
      const uint32_t cfg = p.read(0), io = p.read(4);
      IOLog("ZIZHAN_BT_PROBE: pin=%u cfg=%x func=%u oe=%u pull=%u drive=%u in=%u out=%u\n", pin, cfg, (cfg >> 2) & 15,
            (cfg >> 9) & 1, cfg & 3, (cfg >> 6) & 7, io & 1, (io >> 1) & 1);
    }
    const bool running = !(s5 & 0x80000000u) && !(mahb & 0x80000000u) && !(sahb & 0x80000000u);
    if (!running) {
      IOLog("ZIZHAN_BT_PROBE: DONE se20=clock-off se_read=0 writes=0\n");
    } else {
      Window se;
      if (!se.open(0x894000)) {
        IOLog("ZIZHAN_BT_PROBE: FAIL se-map\n");
      } else {
        const uint32_t fw = se.read(0x68);
        IOLog("ZIZHAN_BT_PROBE: se20 fw=%x protocol=%u if_disable=%x status=%x m_clk=%x clk_sel=%x\n", fw, (fw >> 8) & 255,
              se.read(0x64), se.read(0x40), se.read(0x48), se.read(0x7c));
        IOLog("ZIZHAN_BT_PROBE: DONE se20=running se_read=1 writes=0\n");
      }
    }
  }
  finish();
}

static void start() {
  uint32_t mode = 0;
  if (!PE_parse_boot_argn("zizhan_bt_probe", &mode, sizeof mode) || !mode) return;
  if (mode < 1 || mode > 3) {
    IOLog("ZIZHAN_BT_PROBE: SKIP unsupported-mode=%u\n", mode);
    return;
  }
  probe_mode = mode;
  thread_t thread = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &thread) != KERN_SUCCESS) {
    IOLog("ZIZHAN_BT_PROBE: FAIL worker-start\n");
    return;
  }
  thread_deallocate(thread);
  IOLog("ZIZHAN_BT_PROBE: RUNNING mode=%u readonly=%u\n", mode, mode == 1 ? 1u : 0u);
}
} // namespace zizhan_bt_probe
