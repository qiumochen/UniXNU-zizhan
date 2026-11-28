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
#include "ZizhanBtProtocol.h"

extern "C" struct vnode *rootvnode;
/* BT-01 step 3 (zizhan_bt_probe=4): bsd/dev/zizhan_bt_uart.c, the tty half of /dev/cu.bluetooth. Weak so this header
 * still links into kernels built without that XNU patch (mode 4 then logs SKIP). */
extern "C" int zizhan_bt_uart_attach(int (*tx)(const uint8_t *, uint32_t)) __attribute__((weak));
extern "C" void zizhan_bt_uart_input(const uint8_t *, uint32_t) __attribute__((weak));

/* Set by PongWifiPlatform once the QCA6490's Wi-Fi firmware has started (the two radios share the chip's power). */
extern "C" { volatile int zizhan_wifi_chip_up; }

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
  for (unsigned fast = 0; fast < 25; ++fast) {
    if (se.read(0x610) & 1u) { se.write(0x618, 1u); return true; }   // M_CMD_DONE (~180 us at 3 Mbps)
    IODelay(20);
  }
  const uint64_t end = now_us() + 200000;
  while (now_us() < end) {
    if (se.read(0x610) & 1u) { se.write(0x618, 1u); return true; }   // M_CMD_DONE
    IOSleep(1);
  }
  return false;
}
/* Linux qcom_geni_serial_stop_rx_fifo(): cancel secondary sequencer, wait for S_CMD_CANCEL_EN (BIT(4)),
 * flush any residual RX FIFO words, and fall back to S_CMD_ABORT if S_GENI_CMD_ACTIVE (BIT(12)) persists. */
static void stop_rx_fifo(Window &se) {
  if (!(se.read(0x40) & ZIZHAN_GENI_STATUS_S_ACTIVE)) return;
  se.write(0x634, ZIZHAN_GENI_S_CMD_CANCEL);
  const uint64_t end = now_us() + 50000;
  while (now_us() < end && !(se.read(0x640) & ZIZHAN_GENI_S_IRQ_CMD_CANCEL)) IODelay(50);
  for (unsigned guard = 0; guard < 32; ++guard) {
    const uint32_t words = se.read(0x804) & 0x1ffffffu;
    if (!words) break;
    for (uint32_t w = 0; w < words; ++w) (void)se.read(0x780);
  }
  se.write(0x648, 0xffffffffu);
  if (se.read(0x40) & ZIZHAN_GENI_STATUS_S_ACTIVE) {
    se.write(0x634, ZIZHAN_GENI_S_CMD_ABORT);
    const uint64_t aend = now_us() + 50000;
    while (now_us() < aend && !(se.read(0x640) & ZIZHAN_GENI_S_IRQ_CMD_ABORT)) IODelay(50);
    se.write(0x648, 0xffffffffu);
  }
  se.write(0x618, 0xffffffffu);
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
/* NVM for this polled host: configured baud (3000000 default or 115200 fallback), no in-band sleep, no deep sleep. */
static void nvm_fixup(uint8_t *d, unsigned size, uint32_t baud) {
  unsigned hci = 0, sleep = 0;
  if (!zizhan_bt_nvm_fixup(d, size, baud, &hci, &sleep)) {
    const uint32_t tl = size >= 4 ? (d[0] | (d[1] << 8) | (d[2] << 16) | (uint32_t(d[3]) << 24)) : 0u;
    IOLog("ZIZHAN_BT_PROBE: nvm type=%u untouched\n", tl & 0xffu);
    return;
  }
  IOLog("ZIZHAN_BT_PROBE: nvm fixup hci=%u deep_sleep=%u baud=%u ibs=0\n", hci, sleep, baud);
}

/* Mode 4: the tty's t_oproc copies into this single-producer ring (the tty lock serializes producers); the worker below
 * is the only consumer. tx(nullptr, 0) reports the free space, so the tty only dequeues what fits; after a full ring
 * drains, the worker kicks the tty with zizhan_bt_uart_input(nullptr, 0). */
static bool tx_was_full;
static uint8_t tx_ring[8192];
static uint32_t tx_head, tx_tail;
static int uart_tx_hook(const uint8_t *b, uint32_t n) {
  uint32_t head = __atomic_load_n(&tx_head, __ATOMIC_RELAXED);
  const uint32_t tail = __atomic_load_n(&tx_tail, __ATOMIC_ACQUIRE);
  if (!b) {
    const uint32_t room = uint32_t(sizeof tx_ring) - (head - tail);
    if (!room) {
      __atomic_store_n(&tx_was_full, true, __ATOMIC_RELEASE);
    }
    return int(room);
  }
  uint32_t i = 0;
  for (; i < n && head - tail < sizeof tx_ring; ++i, ++head) {
    tx_ring[head % sizeof tx_ring] = b[i];
  }
  __atomic_store_n(&tx_head, head, __ATOMIC_RELEASE);
  return int(i);
}
/* Host-to-controller H4 framing + vendor/extended-advertising shim (pure-C state machine in ZizhanBtProtocol.h). */
static ZizhanBtH4Tx h4;
static int h4_uart_tx_cb(void *ctx, const uint8_t *data, unsigned len) {
  auto &se = *static_cast<Window *>(ctx);
  if (!tx_bytes(se, data, len)) {
    IOLog("ZIZHAN_BT_PROBE: bridge tx-timeout bytes=%u\n", len);
    return 0;
  }
  return 1;
}
static void h4_host_rx_cb(void *, const uint8_t *data, unsigned len) {
  zizhan_bt_uart_input(data, len);
}
static void h4_log_cmd_cb(void *, uint16_t op, uint8_t plen, int shimmed) {
  IOLog("ZIZHAN_BT_HCI: cmd op=%04x ogf=%x ocf=%03x plen=%u %s\n",
        op, op >> 10, op & 0x3ff, plen, shimmed ? "shimmed" : "forwarded");
}
static void h4_feed(Window &se, const uint8_t *b, unsigned n) {
  const ZizhanBtH4Ops ops = {&se, h4_uart_tx_cb, h4_host_rx_cb, h4_log_cmd_cb};
  zizhan_bt_h4_feed(&h4, &ops, b, n);
}

/* Controller-to-host Supported Commands octet 18 fixup + Qualcomm in-band sleep (IBS) framing. */
static ZizhanBtRxPatch rxp;
static ZizhanBtIbs ibs;
static int ibs_uart_tx_cb(void *ctx, const uint8_t *data, unsigned len) {
  return tx_bytes(*static_cast<Window *>(ctx), data, len) ? 1 : 0;
}
/* After a SLEEP_IND: WAKE_IND, wait up to 3 x 30 ms for WAKE_ACK (or the controller's own WAKE_IND), passing RX through. */
static void ibs_wake(Window &se) {
  uint8_t in[256], out[256];
  ibs.acked = 0;
  ++ibs.wakes;
  for (unsigned tries = 0; tries < 3 && ibs.asleep; ++tries) {
    const uint8_t wake = 0xfd;
    tx_bytes(se, &wake, 1);
    const uint64_t end = now_us() + 30000;
    while (now_us() < end && !ibs.acked && ibs.asleep) {
      const unsigned got = rx_bytes(se, in, sizeof in, 2);
      const unsigned k = got ? zizhan_bt_ibs_rx(&ibs, ibs_uart_tx_cb, &se, in, got, out) : 0;
      if (k) {
        zizhan_bt_rx_patch(&rxp, out, k);
        zizhan_bt_uart_input(out, k);
      }
    }
  }
  if (ibs.asleep) {
    if (ibs.wake_fail++ < 4) {
      IOLog("ZIZHAN_BT_PROBE: ibs wake-timeout (sending anyway)\n");
    }
    ibs.asleep = 0;
  }
}

/* Never returns while the bridge runs: TX ring -> GENI TX FIFO, GENI RX FIFO -> tty, polled every 2 ms when idle. */
static void serve(Window &se) {
  if (!zizhan_bt_uart_attach || !zizhan_bt_uart_input) {
    IOLog("ZIZHAN_BT_PROBE: SKIP bridge kernel-without-bt-uart\n");
    return;
  }
  const int attached = zizhan_bt_uart_attach(uart_tx_hook);
  IOLog("ZIZHAN_BT_PROBE: bridge attach=%d (bluetoothd may now power the radio)\n", attached);
  if (attached != 0) {
    return;
  }
  if (ev_have) {
    zizhan_bt_uart_input(ev_buf, ev_have);
    ev_have = 0;
  }
  uint8_t in[512], out[128];
  uint64_t rx_total = 0, tx_total = 0, last_log = now_us();
  unsigned tx_logged = 0, rx_logged = 0;
  for (;;) {
    uint32_t tail = __atomic_load_n(&tx_tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&tx_head, __ATOMIC_ACQUIRE);
    while (tail != head) {
      unsigned n = 0;
      while (n < sizeof out && tail != head) {
        out[n++] = tx_ring[tail % sizeof tx_ring];
        ++tail;
      }
      if (tx_logged < 8) {   // the first raw host bytes
        ++tx_logged;
        log_bytes("bridge-tx", out, n < 32 ? n : 32);
      }
      if (ibs.asleep) {
        ibs_wake(se);
      }
      h4_feed(se, out, n);
      tx_total += n;
      __atomic_store_n(&tx_tail, tail, __ATOMIC_RELEASE);
    }
    if (__atomic_exchange_n(&tx_was_full, false, __ATOMIC_ACQ_REL)) {
      zizhan_bt_uart_input(nullptr, 0);
    }
    const unsigned got = rx_bytes(se, in, sizeof in, 2);
    if (got) {
      if (rx_logged < 24) { ++rx_logged; log_bytes("bridge-rx", in, got < 32 ? got : 32); }
      uint8_t h4rx[sizeof in];
      const unsigned k = zizhan_bt_ibs_rx(&ibs, ibs_uart_tx_cb, &se, in, got, h4rx);
      if (k) {
        zizhan_bt_rx_patch(&rxp, h4rx, k);
        zizhan_bt_uart_input(h4rx, k);
      }
      rx_total += got;
    }
    if (now_us() - last_log >= 60000000ull) {
      last_log = now_us();
      IOLog("ZIZHAN_BT_PROBE: bridge rx=%llu tx=%llu hci forwarded=%u shimmed=%u cmdmask=%u ibs wake_ind=%u wake_ack=%u sleep_ind=%u wakes=%u fail=%u\n",
            rx_total, tx_total, h4.forwarded, h4.shimmed, rxp.patched, ibs.wake_ind, ibs.wake_ack, ibs.sleep_ind, ibs.wakes, ibs.wake_fail);
    }
  }
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
  // 1. Initial ROM handshake clock rate (115200 baud). QUP RCGs in DFS mode select a bootloader table entry;
  // without DFS, program 7.3728 MHz (GPLL0_OUT_EVEN 300 MHz, M=384 / N=15625, dual edge).
  auto apply_rcg = [](Window &w, const ZizhanBtRcgPlan &plan) -> bool {
    if (plan.dfs_enabled) {
      return true;
    }
    w.write(0x620, plan.rcg_m);
    w.write(0x624, plan.rcg_n);
    w.write(0x628, plan.rcg_d);
    w.write(0x61c, plan.rcg_cfg);
    w.write(0x618, w.read(0x618) | 1u);   // CMD_UPDATE
    bool ok = false;
    for (unsigned i = 0; i < 500 && !ok; ++i) {
      ok = !(w.read(0x618) & 1u);
      if (!ok) IODelay(10);
    }
    return ok;
  };
  const uint32_t dfsr = wrap.read(0x62c);
  const uint32_t hw_early = wrapper.read(0x4);
  const unsigned sampling_early = hw_early >= 0x20050000u ? 16u : 32u;
  uint32_t dfs_perf[8] = {}, dfs_m[8] = {}, dfs_n[8] = {};
  if (dfsr & 1u) {
    for (unsigned l = 0; l < 8; ++l) {
      dfs_perf[l] = wrap.read(0x634 + 4 * l);
      dfs_m[l] = wrap.read(0x674 + 4 * l);
      dfs_n[l] = wrap.read(0x6b4 + 4 * l);
    }
  }
  ZizhanBtRcgPlan init_plan = {};
  const bool have_init = zizhan_bt_plan_rcg(dfsr, dfs_perf, dfs_m, dfs_n, wrap.read(0x61c),
                                            sampling_early, ZIZHAN_BT_BAUD_INIT, &init_plan) != 0;
  const bool updated = have_init ? apply_rcg(wrap, init_plan) : false;
  const int level = have_init ? init_plan.level : -1;
  const uint32_t level_rate = have_init ? init_plan.rate_hz : 0u;
  IOLog("ZIZHAN_BT_PROBE: rate dfs=%u level=%d rate=%u rcg_written=%u\n", dfsr & 1u, level, level_rate, (dfsr & 1u) ? 0u : 1u);
  if (!have_init || level < 0) { IOLog("ZIZHAN_BT_PROBE: SKIP no-exact-uart-rate writes_gcc=0\n"); return; }
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
  bool bridge_ready = false;
  if (probe_mode >= 3) {
    uint8_t *patch = nullptr, *nvm = nullptr;
    unsigned patch_size = 0, nvm_size = 0;
    const bool have = chosen_blob("zizhan-bt-rampatch", patch, patch_size) && chosen_blob("zizhan-bt-nvm", nvm, nvm_size);
    IOLog("ZIZHAN_BT_PROBE: firmware chosen=%u rampatch=%u nvm=%u\n", have ? 1u : 0u, patch_size, nvm_size);
    if (have && patch_size > 16) {
      uint32_t want_baud = ZIZHAN_BT_BAUD_OPER;
      if (PE_parse_boot_argn("zizhan_bt_baud", &want_baud, sizeof want_baud) &&
          want_baud != ZIZHAN_BT_BAUD_INIT && want_baud != ZIZHAN_BT_BAUD_OPER) {
        want_baud = ZIZHAN_BT_BAUD_OPER;
      }
      uint32_t oper_baud = ZIZHAN_BT_BAUD_INIT;
      if (want_baud == ZIZHAN_BT_BAUD_OPER) {
        ZizhanBtRcgPlan oper_plan = {};
        uint8_t edl_baud[5] = {};
        if (zizhan_bt_plan_rcg(dfsr, dfs_perf, dfs_m, dfs_n, wrap.read(0x61c), sampling, ZIZHAN_BT_BAUD_OPER, &oper_plan) &&
            zizhan_bt_edl_set_baud_pkt(ZIZHAN_BT_BAUD_OPER, edl_baud) == 5u) {
          /* Linux hci_qca.c qca_set_speed(QCA_WCN6855) + qcom_geni_serial_set_termios():
           * 1) Hold host RFR NOT_READY (0x80000002) + mask CTS while RX remains active at 115200.
           * 2) Transmit EDL_CHANGE_BAUDRATE at 115200, drain TX shift register, let QCA6490 switch.
           * 3) Cancel 115200 RX via stop_rx_fifo() (waiting for S_CMD_CANCEL_EN so S_GENI_CMD_ACTIVE=0),
           *    program 48 MHz RCG + clk_div=1, restart UART_START_READ, then release manual RFR. */
          se.write(0x25c, ZIZHAN_GENI_TX_CTS_MASK);
          se.write(0x2ac, ZIZHAN_GENI_MANUAL_RFR_NOT_READY);
          IODelay(100);
          const bool baud_sent = tx_bytes(se, edl_baud, sizeof edl_baud);
          for (unsigned i = 0; i < 200 && ((se.read(0x800) & 0x0fffffffu) || (se.read(0x40) & ZIZHAN_GENI_STATUS_M_ACTIVE)); ++i) IODelay(20);
          IODelay(200);
          IOSleep(10);
          stop_rx_fifo(se);
          const bool rcg_ok = baud_sent && apply_rcg(wrap, oper_plan);
          if (rcg_ok) {
            se.write(0x7c, uint32_t(oper_plan.level));
            se.write(0x48, 1u | (oper_plan.clk_div << 4));
            se.write(0x4c, 1u | (oper_plan.clk_div << 4));
            oper_baud = ZIZHAN_BT_BAUD_OPER;
          }
          se.write(0x25c, 0u);
          se.write(0x618, 0xffffffffu);
          se.write(0x648, 0xffffffffu);
          se.write(0x630, 1u << 27);
          IODelay(50);
          se.write(0x2ac, 0u);
          const unsigned drained = rx_bytes(se, rx, sizeof rx, 20);
          if (drained) log_bytes("baud-switch-rx", rx, drained);
          ev_have = 0;
          IOLog("ZIZHAN_BT_PROBE: baud switch want=%u active=%u rate=%u div=%u rcg_ok=%u m_clk=%x s_status=%x ios=%x\n",
                want_baud, oper_baud, oper_plan.rate_hz, oper_plan.clk_div, rcg_ok ? 1u : 0u, se.read(0x48), se.read(0x40), se.read(0x908));
        } else {
          IOLog("ZIZHAN_BT_PROBE: baud switch fallback=115200 (no exact 3Mbps RCG/DFS entry)\n");
        }
      }
      const unsigned mode = patch[14];              // tlv_type_patch.download_mode
      const uint64_t t_patch = now_us();
      bool ok = download(se, "rampatch", patch, patch_size, mode);
      const uint64_t rampatch_ms = (now_us() - t_patch) / 1000ull;
      IOLog("ZIZHAN_BT_PROBE: rampatch timing baud=%u elapsed_ms=%llu ok=%u\n", oper_baud, (unsigned long long)rampatch_ms, ok ? 1u : 0u);
      IOSleep(10);
      if (ok) { nvm_fixup(nvm, nvm_size, oper_baud); ok = download(se, "nvm", nvm, nvm_size, 0); }
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
        bridge_ready = true;
      }
    }
    if (patch) IOFree(patch, patch_size);
    if (nvm) IOFree(nvm, nvm_size);
  }
  if (probe_mode == 4 && bridge_ready) {
    serve(se);   // returns only when the bridge could not attach; then fall through to the normal cleanup
  }
  // 6. Cleanup: stop RX, BT_EN low, pins back. Clock votes stay (never removed, as SE15).
  stop_rx_fifo(se);
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
  // After Wi-Fi bring-up, which shares the chip's power: 2 s after its firmware started, or the old fixed 30 s when Wi-Fi is
  // off or late (Bluetooth used to be ready ~26 s after launchd wanted it). zizhan_bt_wait_ms=<n> forces a fixed wait.
  uint32_t fixed = 0;
  if (PE_parse_boot_argn("zizhan_bt_wait_ms", &fixed, sizeof fixed) && fixed >= 1000 && fixed <= 120000) {
    IOSleep(fixed);
  } else {
    unsigned waited = 0;
    for (; waited < 30000 && !__atomic_load_n(&zizhan_wifi_chip_up, __ATOMIC_ACQUIRE); waited += 100) {
      IOSleep(100);
    }
    if (waited < 28000) {
      IOSleep(2000);
    }
    IOLog("ZIZHAN_BT_PROBE: start %u ms after the root mount (wifi up=%d)\n", waited, zizhan_wifi_chip_up);
  }
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
  if (mode < 1 || mode > 4) {
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
