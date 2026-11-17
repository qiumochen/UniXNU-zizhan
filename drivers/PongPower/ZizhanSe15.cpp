/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include "GeniI2c.h"
#include "ZizhanSe15State.h"

namespace {
struct ReadPage {
    IOMemoryMap *map = nullptr;
    volatile uint8_t *va = nullptr;
    bool open(uint64_t base, uint64_t bytes) {
        auto memory = IODeviceMemory::withRange(base, bytes);
        map = memory ? memory->map(kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly) : nullptr;
        if (memory) memory->release();
        va = map ? reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress()) : nullptr;
        return va != nullptr;
    }
    uint32_t read(unsigned offset) {
        uint32_t value = *reinterpret_cast<volatile uint32_t *>(va + offset);
        __asm__ volatile("dmb oshld" ::: "memory"); return value;
    }
    ~ReadPage() { if (map) map->release(); }
};
bool board() {
    auto node = IORegistryEntry::fromPath("/arm-io/pong-touch", gIODTPlane);
    if (!node) return false;
    auto data = OSDynamicCast(OSData, node->getProperty("compatible"));
    const char identity[] = "st,fts-zizhan-pri";
    const bool matched = data && data->getLength() == sizeof identity &&
        !memcmp(data->getBytesNoCopy(), identity, sizeof identity);
    node->release(); return matched;
}
void worker(void *, wait_result_t) {
    thread_set_thread_name(current_thread(), "zizhan_se15");
    // Each GPIO and GCC page is read-only; never acquire the controller's
    // transaction ownership or call geni_i2c_prepare/PongAudio setup.
    ReadPage votes, gates, sda, scl, reset;
    if (!votes.open(0x162000, 0x1000) || !gates.open(0x12e000, 0x1000) ||
        !sda.open(0xf138000, 0x1000) || !scl.open(0xf139000, 0x1000) ||
        !reset.open(0xf101000, 0x1000)) {
        IOLog("ZIZHAN_SE15: FAIL resource-map readonly=1 finished=1\n"); return;
    }
    const zizhan_se15::Snapshot s = {
        votes.read(0x10), gates.read(0xc), gates.read(4), gates.read(8),
        sda.read(0), scl.read(0), reset.read(0), reset.read(4)};
    const auto reason = zizhan_se15::check(s);
    IOLog("ZIZHAN_SE15: snapshot votes=%x gates=%x/%x/%x pins=%x/%x reset=%x/%x readonly=1\n",
          s.votes, s.se_gate, s.master_gate, s.slave_gate, s.sda_config, s.scl_config, s.reset_config, s.reset_io);
    if (reason != zizhan_se15::Ready) {
        IOLog("ZIZHAN_SE15: SKIP prerequisite=%u se_mapped=0 i2c_commands=0 finished=1\n", unsigned(reason)); return;
    }
    ReadPage engine;
    if (!engine.open(0x880000, 0x4000)) {
        IOLog("ZIZHAN_SE15: FAIL engine-map i2c_commands=0 finished=1\n"); return;
    }
    const uint32_t fw = engine.read(GENI_FW_REVISION), config = engine.read(GENI_CLK_CFG);
    const uint32_t disable = engine.read(GENI_IF_DISABLE), timing = engine.read(GENI_SCL_COUNTERS);
    const uint32_t status = engine.read(0x40), irq = engine.read(GENI_M_IRQ_STATUS);
    const uint32_t dma = engine.read(GENI_DMA_MODE);
    IOLog("ZIZHAN_SE15: engine fw=%x clk=%x disable=%x timing=%x status=%x irq=%x dma=%x readonly=1\n",
          fw, config, disable, timing, status, irq, dma);
    // A live bus/clock is not amplifier rail readiness or an exclusive I2C
    // lease. This first physical prerequisite observation stops here.
    IOLog("ZIZHAN_SE15: SKIP supply-and-controller-lease-unverified i2c_commands=0 amp_identity=UNTESTED finished=1\n");
}
} // namespace
extern "C" void zizhan_se15_probe(void) {
    unsigned mode = 0;
    if (!PE_parse_boot_argn("zizhan_se15_observe", &mode, sizeof mode) || mode != 1) return;
    unsigned audio = 0; PE_parse_boot_argn("pong_audio", &audio, sizeof audio);
    if (audio || !board()) { IOLog("ZIZHAN_SE15: SKIP board-or-audio-conflict finished=1\n"); return; }
    static uint32_t started;
    if (__atomic_exchange_n(&started, 1, __ATOMIC_ACQ_REL)) return;
    IOLog("ZIZHAN_SE15: RUNNING readonly-prerequisites no-clock-reset-pin-writes=1\n");
    thread_t thread = nullptr;
    if (kernel_thread_start(worker, nullptr, &thread) != KERN_SUCCESS) {
        IOLog("ZIZHAN_SE15: FAIL thread-create finished=1\n"); return;
    }
    thread_deallocate(thread);
}
