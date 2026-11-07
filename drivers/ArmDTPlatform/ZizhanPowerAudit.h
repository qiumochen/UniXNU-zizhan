// SPDX-License-Identifier: MIT
// zizhan POWER-02: default-off, finite registry/PSCI query audit.
// No MMIO, device power, suspend, wakealarm or firmware mode writes.
#pragma once
#include <IOKit/IORegistryEntry.h>
#include <kern/thread.h>
#include <string.h>
#include "ZizhanPowerQuery.h"
#include "ZizhanPowerRequest.h"

extern "C" struct vnode *rootvnode;
extern "C" uint32_t psci_smp_hvc;

namespace zizhan_power_audit {
static volatile uint32_t request_slots;
static uint64_t query(bool hvc, uint32_t fid, uint32_t argument) {
    register uint64_t x0 __asm__("x0") = fid;
    register uint64_t x1 __asm__("x1") = argument;
    register uint64_t x2 __asm__("x2") = 0;
    register uint64_t x3 __asm__("x3") = 0;
    if (hvc)
        __asm__ volatile("hvc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : :
            "x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","cc","memory");
    else
        __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : :
            "x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","cc","memory");
    return x0;
}
static void registry() {
    IOLog("ZIZHAN_POWER: registry root-domain-present=%u contract=UNQUALIFIED\n",
          IOService::getPMRootDomain() ? 1u : 0u);
    IORegistryIterator *it = IORegistryIterator::iterateOver(gIOServicePlane, kIORegistryIterateRecursively);
    if (!it) {IOLog("ZIZHAN_POWER: registry SKIP iterator-unavailable\n"); return;}
    unsigned visited = 0, printed = 0;
    OSObject *obj;
    while (visited < 256 && printed < 16 && (obj = it->getNextObject()) != nullptr) {
        ++visited;
        IORegistryEntry *entry = OSDynamicCast(IORegistryEntry, obj);
        const char *cls = entry ? entry->getMetaClass()->getClassName() : nullptr;
        if (cls && (!strcmp(cls,"ArmDTPlatform") || !strcmp(cls,"ArmCpu") ||
                    !strcmp(cls,"PongWifi") || !strcmp(cls,"PongAdreno") ||
                    !strcmp(cls,"IOMobileFramebuffer") || !strcmp(cls,"IOPMrootDomain") ||
                    !strcmp(cls,"AppleSmartBattery"))) {
            IOLog("ZIZHAN_POWER: registry owner=%s power-contract=UNQUALIFIED\n",cls);
            ++printed;
        }
    }
    it->release();
    IOLog("ZIZHAN_POWER: registry observed=%u owners=%u limit=256/16 inventory-only=1\n",
          visited, printed);
}
static void worker(void *arg, wait_result_t) {
    const bool hvc = reinterpret_cast<uintptr_t>(arg) != 0;
    for (unsigned i = 0; i < 1200 && !__atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE); ++i)
        IOSleep(100);
    if (!__atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE)) {
        IOLog("ZIZHAN_POWER: SKIP root-timeout finished=1\n");
    } else {
        IOSleep(5000);
        registry();
        auto o = zizhan_power_query::observe([hvc](uint32_t fid,uint32_t value) {
            return query(hvc,fid,value);
        });
        IOLog("ZIZHAN_POWER: psci version-raw=0x%llx version=%u.%u conduit=%s\n",
              o.raw_version,o.version>>16,o.version&0xffff,hvc?"hvc":"smc");
        const zizhan_power_query::Feature *fields[] = {&o.api,&o.cpu,&o.system,&o.mode};
        for (auto f : fields) if (f->function)
            IOLog("ZIZHAN_POWER: psci feature=0x%x raw=0x%llx result=%d supported=%u\n",
                  f->function,f->raw,f->result,f->result>=0?1u:0u);
        if (o.cpu.function && o.cpu.result >= 0)
            IOLog("ZIZHAN_POWER: psci cpu-suspend format-extended=%u osi-advertised=%u state-ids=UNQUALIFIED\n",
                  (uint32_t(o.cpu.result)>>1)&1u,uint32_t(o.cpu.result)&1u);
        const char *status=o.state==zizhan_power_query::State::Pass?"PASS":
            o.state==zizhan_power_query::State::FailFeatureReply?"FAIL":"SKIP";
        IOLog("ZIZHAN_POWER: %s query-only=1 queries=%u suspend-calls=0 mode-writes=0 finished=1\n",
              status,o.queries);
    }
    thread_terminate(current_thread());
    thread_block(THREAD_CONTINUE_NULL);
}
static void start(IOService *provider) {
    uint32_t enabled = 0;
    if (!PE_parse_boot_argn("zizhan_power_audit",&enabled,sizeof(enabled)) || !enabled) return;
    if (enabled != 1) {IOLog("ZIZHAN_POWER: SKIP unsupported-mode\n"); return;}
    IORegistryEntry *chosen = provider->childFromPath("chosen",gIODTPlane);
    OSData *data = chosen ? OSDynamicCast(OSData,chosen->getProperty("psci-method")) : nullptr;
    bool smc = data && data->getLength()==4 && !memcmp(data->getBytesNoCopy(),"smc",4);
    bool hvc = data && data->getLength()==4 && !memcmp(data->getBytesNoCopy(),"hvc",4);
    if (chosen) chosen->release();
    if ((!smc && !hvc) || (__atomic_load_n(&psci_smp_hvc,__ATOMIC_ACQUIRE)!=uint32_t(hvc))) {
        IOLog("ZIZHAN_POWER: SKIP missing-or-mismatched-established-conduit\n"); return;
    }
    thread_t thread;
    if (kernel_thread_start(worker,reinterpret_cast<void *>(uintptr_t(hvc)),&thread)!=KERN_SUCCESS) {
        IOLog("ZIZHAN_POWER: FAIL worker-start\n"); return;
    }
    IOLog("ZIZHAN_POWER: RUNNING query-only=1 executor=ABSENT\n");
    thread_deallocate(thread);
}
} // namespace zizhan_power_audit

extern "C" void zizhan_display_power_observe(uint32_t count,const uint64_t *values,size_t bytes) {
    static volatile uint32_t mode = 2;
    uint32_t enabled = __atomic_load_n(&mode,__ATOMIC_RELAXED);
    if (enabled == 2) {
        enabled = 0;
        PE_parse_boot_argn("zizhan_power_observe",&enabled,sizeof(enabled));
        __atomic_store_n(&mode,enabled==1?1u:0u,__ATOMIC_RELAXED);
    }
    if (enabled != 1) return;
    uint32_t slot=zizhan_power_request::reserve(&zizhan_power_audit::request_slots);
    if (slot > 32) return;
    if (slot == 32) {IOLog("ZIZHAN_POWER_REQUEST: SKIP output-limit=32\n");return;}
    if (count && !values) {IOLog("ZIZHAN_POWER_REQUEST: FAIL malformed-scalars\n");return;}
    IOLog("ZIZHAN_POWER_REQUEST: SKIP observe-only=1 sequence=%u scalars=%u structure=%lu raw=0x%llx/0x%llx/0x%llx hardware-transition=UNIMPLEMENTED\n",
          slot+1,count,(unsigned long)bytes,count?values[0]:0,count>1?values[1]:0,count>2?values[2]:0);
}
