/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
#include "ZizhanCmdDbCapture.h"
#include "ZizhanRpmhReceipt.h"
namespace {
bool board() {
    auto node = IORegistryEntry::fromPath("/arm-io/pong-touch", gIODTPlane);
    auto compatible = node ? OSDynamicCast(OSData, node->getProperty("compatible")) : nullptr;
    auto base = node ? OSDynamicCast(OSData, node->getProperty("cmd-db-base")) : nullptr;
    const char identity[] = "st,fts-zizhan-pri";
    uint64_t physical = 0;
    if (base && base->getLength() == sizeof physical) memcpy(&physical, base->getBytesNoCopy(), sizeof physical);
    const bool valid = compatible && compatible->getLength() == sizeof identity &&
        !memcmp(compatible->getBytesNoCopy(), identity, sizeof identity) && physical == 0x80860000;
    if (node) node->release();
    return valid;
}
struct ReadRegion {
    IOMemoryMap *map = nullptr;
    volatile uint32_t *words = nullptr;
    bool open() {
        auto memory = IODeviceMemory::withRange(0x80860000, zizhan_cmd_db::Bytes);
        map = memory ? memory->map(kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly) : nullptr;
        if (memory) memory->release();
        words = map ? reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress()) : nullptr;
        return words != nullptr;
    }
    uint32_t read32(unsigned at) {
        const uint32_t value = words[at / 4];
        __asm__ volatile("dmb oshld" ::: "memory");
        return value;
    }
    ~ReadRegion() { if (map) map->release(); }
};
uint64_t nowUs() { uint64_t ns = 0; absolutetime_to_nanoseconds(mach_absolute_time(), &ns); return ns / 1000; }
void worker(void *context, wait_result_t) {
    const unsigned mode=unsigned(reinterpret_cast<uintptr_t>(context));
    if(mode==2)IOSleep(10000); // One bounded delay for existing boot owners; no resource request.
    thread_set_thread_name(current_thread(), "zizhan_cmd_db");
    ReadRegion region;
    if (!region.open()) { IOLog("ZIZHAN_CMD_DB: FAIL readonly-map finished=1\n"); return; }
    auto *a = static_cast<uint8_t *>(IOMalloc(zizhan_cmd_db::Bytes));
    auto *b = static_cast<uint8_t *>(IOMalloc(zizhan_cmd_db::Bytes));
    if (!a || !b) {
        if (a) IOFree(a, zizhan_cmd_db::Bytes);
        if (b) IOFree(b, zizhan_cmd_db::Bytes);
        IOLog("ZIZHAN_CMD_DB: FAIL allocation finished=1\n"); return;
    }
    zizhan_cmd_db::Receipt receipt;
    const auto result = zizhan_cmd_db::capture(1, region, nowUs, a, b, zizhan_cmd_db::Bytes, receipt);
    IOLog("ZIZHAN_CMD_DB: capture result=%u copies=%u words=%u parser=%d resources=%u readonly=1 votes=0 slpi_start=0\n",
          unsigned(result), receipt.copies, receipt.words, receipt.parser_result, receipt.plan.count);
    if (result == zizhan_cmd_db::Ok) {
        for (unsigned i = 0; i < receipt.plan.count; ++i) {
            const auto &r = receipt.plan.commands[i];
            IOLog("ZIZHAN_CMD_DB: resource=%s address=%x value=%x kind=%u vcd=%u planned_only=1\n",
                  r.name, r.address, r.value, unsigned(r.kind), unsigned(r.vcd));
        }
    }
    if(mode==2&&result==zizhan_cmd_db::Ok) {
        unsigned known=0;
        for(unsigned i=0;i<receipt.plan.count;++i) {
            const auto &resource=receipt.plan.commands[i];zizhan_rpmh::CompletedRequest actual={};
            const bool found=pong_rpmh_completed_request(resource.address,&actual);
            if(found)++known;
            IOLog("ZIZHAN_SLPI_REQUEST: resource=%s address=%x observed=%u request=%x sequence=%llu inherited_baseline=0 votes=0\n",resource.name,resource.address,found?1u:0u,actual.value,(unsigned long long)actual.sequence);
        }
        IOLog("ZIZHAN_SLPI_REQUEST: PASS queried=%u known=%u unknown=%u software_completion_only=1 atomic_snapshot=0 inherited_baseline=0 votes=0 slpi_start=0 finished=1\n",receipt.plan.count,known,receipt.plan.count-known);
    }
    if (mode==1&&(result == zizhan_cmd_db::Ok || result == zizhan_cmd_db::Invalid)) {
        // A stable raw table is useful even when the parser rejects its
        // actual format. Keep that terminal FAIL and zero plan; raw data
        // alone never becomes a valid resource or trusted baseline.
        // Full actual table for private host hash/parser cross-check. Exactly
        //2048 records of64bytes, no firmware/OTP or device identifiers.
        // Do not publish raw device logs or confuse this plan with a vote.
        uint64_t checksum = 14695981039346656037ULL;
        for (unsigned i = 0; i < zizhan_cmd_db::Bytes; ++i) { checksum ^= a[i]; checksum *= 1099511628211ULL; }
        IOLog("ZIZHAN_CMD_DB: raw bytes=131072 chunks=2048 fnv64=%016llx stable=1\n", (unsigned long long)checksum);
        const char hex[] = "0123456789abcdef";
        for (unsigned at = 0; at < zizhan_cmd_db::Bytes; at += 64) {
            char line[129];
            for (unsigned i = 0; i < 64; ++i) {
                line[2 * i] = hex[a[at + i] >> 4]; line[2 * i + 1] = hex[a[at + i] & 15];
            }
            line[128] = 0;
            IOLog("ZIZHAN_CMD_DB_DATA: offset=%u hex=%s\n", at, line);
            if (!(at % 4096)) IOSleep(1);
        }
    }
    IOFree(a, zizhan_cmd_db::Bytes); IOFree(b, zizhan_cmd_db::Bytes);
    IOLog("ZIZHAN_CMD_DB: %s result=%u readonly=1 votes=0 baseline_verified=0 slpi_start=0 finished=1\n",
          result == zizhan_cmd_db::Ok ? "PASS snapshot" : "FAIL snapshot", unsigned(result));
}
} // namespace
extern "C" void zizhan_cmd_db_probe(void) {
    unsigned mode = 0;
    if (!PE_parse_boot_argn("zizhan_cmd_db_observe", &mode, sizeof mode) || (mode != 1&&mode != 2)) return;
    if (!board()) { IOLog("ZIZHAN_CMD_DB: SKIP board-resource finished=1\n"); return; }
    static uint32_t started;
    if (__atomic_exchange_n(&started, 1, __ATOMIC_ACQ_REL)) return;
    IOLog("ZIZHAN_CMD_DB: RUNNING base=80860000 bytes=20000 readonly=1 votes=0\n");
    thread_t thread = nullptr;
    if (kernel_thread_start(worker, reinterpret_cast<void *>(uintptr_t(mode)), &thread) != KERN_SUCCESS) {
        IOLog("ZIZHAN_CMD_DB: FAIL thread-create finished=1\n"); return;
    }
    thread_deallocate(thread);
}
