/* SPDX-License-Identifier: BSD-2-Clause-Patent */
// Opt-in single APSS IPCC receiver for zizhan ADSP/SLPI consumers. Does not
// start a DSP or alter PAS, clocks, rails, SMEM or GLINK state.
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>
#include "QcomScm.h"
#include "ZizhanIpcc.h"
#include "ZizhanIpccRouter.h"

extern "C" int armcpu_spi_attach(uint32_t, bool, void (*)(void *), void *, uint32_t *, uint64_t *) __attribute__((weak_import));
extern "C" { volatile uint32_t zizhan_ipcc_stats[8]; }
namespace {
zizhan_ipcc::Router router;
volatile uint32_t owner_state, writer;
int owner_error;
IOMemoryMap *register_map;
volatile uint8_t *registers;
uint64_t report_last_ns;
unsigned reports;
// state: 0 not initialized, 1 initializing, 2 attached, 3 failed permanently.
uint32_t read32(unsigned offset) {
    const uint32_t value = *reinterpret_cast<volatile uint32_t *>(registers + offset);
    __asm__ volatile("dmb oshld" ::: "memory");
    return value;
}
void write32(unsigned offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t *>(registers + offset) = value;
    __asm__ volatile("dsb oshst" ::: "memory");
}
bool valid_inputs() {
    auto chosen = IORegistryEntry::fromPath("/chosen", gIODTPlane);
    auto data = chosen ? OSDynamicCast(OSData, chosen->getProperty("zizhan-ipcc-v1")) : nullptr;
    uint64_t base = 0;
    uint32_t values[2]{};
    bool valid = data && data->getLength() == 16;
    if (valid) {
        const auto raw = static_cast<const uint8_t *>(data->getBytesNoCopy());
        memcpy(&base, raw, 8); memcpy(values, raw + 8, 8);
        valid = base == 0x0ed18000 && values[0] == 0x1000 && values[1] == 261;
    }
    if (chosen) chosen->release();
    return valid;
}
void interrupt(void *) {
    const auto result = zizhan_ipcc::drain(router,
        [] { return read32(QCOM_IPCC_RECV_ID); },
        [](uint32_t id) { write32(QCOM_IPCC_RECV_CLEAR, id); });
    __atomic_fetch_add(&zizhan_ipcc_stats[1], 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&zizhan_ipcc_stats[2], result.received, __ATOMIC_RELAXED);
    __atomic_fetch_add(&zizhan_ipcc_stats[3], result.delivered, __ATOMIC_RELAXED);
    __atomic_fetch_add(&zizhan_ipcc_stats[4], result.unknown, __ATOMIC_RELAXED);
    if (result.budget_used) __atomic_fetch_add(&zizhan_ipcc_stats[5], 1, __ATOMIC_RELAXED);
}
int attach(uint32_t id, zizhan_ipcc::Callback callback, void *context) {
    uint32_t state = __atomic_load_n(&owner_state, __ATOMIC_ACQUIRE);
    if (state == 2) return kIOReturnSuccess;
    if (state == 3) return owner_error;
    uint32_t empty = 0;
    if (!__atomic_compare_exchange_n(&owner_state, &empty, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return kIOReturnBusy;
    if (!valid_inputs() || !armcpu_spi_attach) {
        owner_error = kIOReturnBadArgument;
        __atomic_store_n(&owner_state, 3, __ATOMIC_RELEASE);
        IOLog("ZIZHAN_IPCC: SKIP invalid-board-inputs-or-no-gic\n");
        return owner_error;
    }
    auto memory = IODeviceMemory::withRange(0x0ed18000, 0x1000);
    register_map = memory ? memory->map(kIOMapAnywhere | kIOMapInhibitCache) : nullptr;
    if (memory) memory->release();
    registers = register_map ? reinterpret_cast<volatile uint8_t *>(register_map->getVirtualAddress()) : nullptr;
    if (!registers) {
        if (register_map) register_map->release();
        register_map = nullptr;
        owner_error = kIOReturnNoMemory;
        __atomic_store_n(&owner_state, 3, __ATOMIC_RELEASE);
        return kIOReturnNoMemory;
    }
    // Publish the first consumer before GIC can call interrupt(). Otherwise
    // a pending doorbell at enable could be acknowledged as unknown.
    const auto first = zizhan_ipcc::subscribe(router, id, callback, context);
    if (first != zizhan_ipcc::Result::Ok && first != zizhan_ipcc::Result::Duplicate) {
        register_map->release(); register_map = nullptr; registers = nullptr;
        owner_error = kIOReturnExclusiveAccess;
        __atomic_store_n(&owner_state, 3, __ATOMIC_RELEASE);
        return owner_error;
    }
    if (first == zizhan_ipcc::Result::Ok)
        __atomic_fetch_add(&zizhan_ipcc_stats[6], 1, __ATOMIC_RELAXED);
    const int result = armcpu_spi_attach(261, false, interrupt, nullptr, nullptr, nullptr);
    if (result) {
        register_map->release(); register_map = nullptr; registers = nullptr;
        owner_error = result;
        // GIC-not-ready can be retried by the bounded calling worker.
        __atomic_store_n(&owner_state, result == kIOReturnNotReady ? 0u : 3u, __ATOMIC_RELEASE);
        const uint32_t attempt = __atomic_fetch_add(&zizhan_ipcc_stats[7], 1, __ATOMIC_RELAXED);
        if (result != kIOReturnNotReady || attempt < 4)
            IOLog("ZIZHAN_IPCC: %s attach=%d\n", result == kIOReturnNotReady ? "SKIP" : "FAIL", result);
        return result;
    }
    zizhan_ipcc_stats[0] = 1;
    __atomic_store_n(&owner_state, 2, __ATOMIC_RELEASE);
    IOLog("ZIZHAN_IPCC: PASS receiver-attached spi=261; no-DSP-start\n");
    return kIOReturnSuccess;
}
} // namespace

extern "C" int zizhan_ipcc_routing_enabled(void) {
    uint32_t mode = 0;
    return PE_parse_boot_argn("zizhan_ipcc_route", &mode, sizeof(mode)) && mode == 1;
}

extern "C" int zizhan_ipcc_subscribe(uint32_t id, void (*callback)(void *, uint32_t), void *context) {
    if (!zizhan_ipcc_routing_enabled()) return kIOReturnUnsupported;
    // Only the five reviewed live-DT doorbells are supported in this increment.
    if (!callback || (id != 0 && id != 0x30000 && id != 0x30002 && id != 0x40000 && id != 0x40002))
        return kIOReturnBadArgument;
    uint32_t empty = 0;
    if (!__atomic_compare_exchange_n(&writer, &empty, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return kIOReturnBusy;
    const int result = attach(id, callback, context);
    if (result) {
        __atomic_store_n(&writer, 0, __ATOMIC_RELEASE);
        return result;
    }
    const auto added = zizhan_ipcc::subscribe(router, id, callback, context);
    if (added == zizhan_ipcc::Result::Ok || added == zizhan_ipcc::Result::Duplicate) {
        // RECV_ENABLE is an idempotent per-id command, also needed after a
        // first subscription survived a GIC-not-ready attempt.
        write32(QCOM_IPCC_RECV_ENABLE, id);
    }
    if (added == zizhan_ipcc::Result::Ok) {
        __atomic_fetch_add(&zizhan_ipcc_stats[6], 1, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&writer, 0, __ATOMIC_RELEASE);
    if (added == zizhan_ipcc::Result::Ok || added == zizhan_ipcc::Result::Duplicate)
        return kIOReturnSuccess;
    return added == zizhan_ipcc::Result::Conflict ? kIOReturnExclusiveAccess : kIOReturnNoResources;
}

extern "C" void zizhan_ipcc_observe(uint64_t now_ns, uint32_t adsp_callbacks) {
    // Single caller: the ADSP worker. Counter loads are individually atomic;
    // an IRQ can update them between loads, so this is not a coherent latch.
    if (!zizhan_ipcc_routing_enabled() ||
        __atomic_load_n(&owner_state, __ATOMIC_ACQUIRE) != 2 || reports >= 8) return;
    if (reports && (now_ns < report_last_ns || now_ns - report_last_ns < 10000000000ULL)) return;
    report_last_ns = now_ns;
    uint32_t values[8]{};
    for (unsigned i = 0; i < 8; ++i)
        values[i] = __atomic_load_n(&zizhan_ipcc_stats[i], __ATOMIC_RELAXED);
    uint32_t delivered[5]{};
    const uint32_t ids[5] = {0, 0x30000, 0x30002, 0x40000, 0x40002};
    const uint32_t used = __atomic_load_n(&router.used, __ATOMIC_ACQUIRE);
    for (uint32_t i = 0; i < used; ++i)
        for (unsigned j = 0; j < 5; ++j)
            if (router.slots[i].id == ids[j])
                delivered[j] = __atomic_load_n(&router.slots[i].delivered, __ATOMIC_RELAXED);
    IOLog("ZIZHAN_IPCC_STATS: sample=%u at_ns=%llu irq=%u received=%u delivered=%u unknown=%u budget=%u subscriptions=%u retries=%u adsp_callbacks=%u per_id=%u/%u/%u/%u/%u atomic_fields=1 coherent_latch=0\n",
          ++reports, static_cast<unsigned long long>(now_ns), values[1], values[2], values[3],
          values[4], values[5], values[6], values[7], adsp_callbacks,
          delivered[0], delivered[1], delivered[2], delivered[3], delivered[4]);
}
