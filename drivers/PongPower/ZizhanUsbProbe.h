/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Tier 3.2 (USB-01): Timeboxed read-only Synopsys DWC3 (ssusb@a600000) & USB3_GDSC (0x149004)
 * state inventory probe, default OFF (zizhan_usb_probe=1).
 * Strict safety invariant: ZERO writes, ZERO PHY resets — preserves 100% reliable fastboot return! */
#pragma once
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>

namespace zizhan_usb_probe {

struct RoWin {
  IOMemoryMap *map = nullptr;
  volatile uint8_t *va = nullptr;
  bool open(uint64_t pa, size_t len) {
    IODeviceMemory *m = IODeviceMemory::withRange(pa, len);
    map = m ? m->map(kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly) : nullptr;
    if (m) m->release();
    va = map ? reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress()) : nullptr;
    return va != nullptr;
  }
  uint32_t r(size_t off) const {
    const uint32_t v = *reinterpret_cast<const volatile uint32_t *>(va + off);
    __asm__ volatile("dmb oshld" ::: "memory");
    return v;
  }
  ~RoWin() { if (map) map->release(); }
};

static uint32_t gUsbMode;

static uint64_t usbCntfrq() {
  uint64_t f = 0;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
  return f ? f : 19200000ull;
}

static uint64_t usbCntvct() {
  uint64_t t = 0;
  __asm__ volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(t));
  return t;
}

static void worker(void *, wait_result_t) {
  IOSleep(13000);
  uint32_t gdsc = 0;
  bool ok = false;
  {
    RoWin gccUsb;
    ok = gccUsb.open(0x00149000ull, 0x1000u);
    if (ok) {
      /* Verified in SM8475 pong.dts: qcom,gdsc@149004 { reg = <0x149004 0x04>; regulator-name = "gcc_usb30_prim_gdsc"; }
       * Do not read unverified sub-offsets or gated ssusb@a600000 MMIO after UEFI ExitBootServices. */
      gdsc = gccUsb.r(0x004u);
    }
  }
  if (!ok) {
    IOLog("ZIZHAN_USB_PROBE: FAIL gcc-map\n");
  } else {
    const bool pwrOn = (gdsc & 0x80000000u) != 0u;
    const bool swCollapse = (gdsc & 0x00000001u) != 0u;
    const bool gdscOn = pwrOn && !swCollapse;
    const uint64_t stepTicks = 180ull * usbCntfrq();
    for (unsigned rep = 0; rep < 6u; ++rep) {
      IOLog("ZIZHAN_USB_PROBE: PASS mode=%u gdsc=0x%x(pwr_on=%u,sw_collapse=%u,active=%u) dwc3=0xa600000(eusb2+ssp,role-switch) writes=0\n",
            gUsbMode, gdsc, pwrOn ? 1u : 0u, swCollapse ? 1u : 0u, gdscOn ? 1u : 0u);
      const uint64_t lastLog = usbCntvct();
      while (usbCntvct() - lastLog < stepTicks) {
        IOSleep(5000);
      }
    }
  }
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}

static void start() {
  uint32_t mode = 0;
  if (!PE_parse_boot_argn("zizhan_usb_probe", &mode, sizeof(mode)) || (mode != 1u && mode != 2u)) return;
  gUsbMode = mode;
  thread_t th = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &th) == KERN_SUCCESS) thread_deallocate(th);
}

} // namespace zizhan_usb_probe
