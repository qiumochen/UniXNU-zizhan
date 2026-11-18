// SPDX-License-Identifier: BSD-2-Clause-Patent
// Shared by PongAudioService.cpp and ZizhanAudioService.cpp (moved unchanged from PongAudioService.cpp).
#ifndef SHENG_AUDIO_HOST_CLOCK_H
#define SHENG_AUDIO_HOST_CLOCK_H
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <kern/clock.h>
extern "C" vm_offset_t phystokv(uint64_t pa);
extern "C" uint64_t pmap_find_pa(void *pmap, uint64_t va);
// Darwin24 keeps this XNU function in Unsupported.exports; the generated
// kext header omits its declaration. Its task_t -> pmap_t ABI is unchanged.
extern "C" struct pmap *get_task_pmap(task_t task);

// CoreAudio18 loads the Apple 24 MHz host frequency from the dyld shared
// cache. Pong's architectural counter is 19.2 MHz. Adapt just that double,
// following Sheng's clock handoff, after resolving it in the caller's pmap.
static IOReturn setHostClock(task_t task,uint64_t address) {
    constexpr uint64_t apple=0x4176e36000000000ULL;
    if(task!=current_task() || (address&7) || address<0x180000000ULL || address>=0x400000000ULL)
        return kIOReturnBadArgument;
    auto memory=IOMemoryDescriptor::withAddressRange(address,8,kIODirectionOut,task);
    if(!memory)return kIOReturnNoMemory;
    IOReturn r=memory->prepare();
    if(r!=kIOReturnSuccess){memory->release();return r;}
    uint64_t seen=0;
    if(copyin(address,&seen,8))r=kIOReturnNotReadable;
    else {
        uint64_t pa=pmap_find_pa(get_task_pmap(task),address);
        r=kIOReturnNoResources;
        if(pa) {
            mach_timebase_info_data_t tb;clock_timebase_info(&tb);
            uint64_t hz=1000000000ULL*tb.denom/tb.numer;
            unsigned top=63-(unsigned)__builtin_clzll(hz);
            if(top<=52) {
                uint64_t bits=(uint64_t)(1023+top)<<52|((hz<<(52-top))&((1ULL<<52)-1));
                auto value=reinterpret_cast<volatile uint64_t *>(phystokv(pa));
                r=kIOReturnUnsupported;
                if(*value==seen && (seen==apple || seen==bits)) {
                    *value=bits;__asm__ volatile("dsb sy":::"memory");r=kIOReturnSuccess;
                    IOLog("PongAudio: CoreAudio host clock=%llu Hz\n",hz);
                }
            }
        }
    }
    memory->complete();memory->release();return r;
}
#endif
