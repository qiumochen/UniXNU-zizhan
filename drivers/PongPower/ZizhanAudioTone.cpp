/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <mach/vm_param.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
#include "ZizhanAudioDmaMap.h"
#include "ZizhanAudioTone.h"
extern "C" boolean_t pmap_valid_page(ppnum_t);
namespace {
unsigned phase=0;uint64_t owned_base=0;
uint64_t tone_us(){uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000;}
bool access(bool clear){
 const uint64_t tagged=zizhan_audio_dma_map_address(),base=uint32_t(tagged);
 if((tagged>>32)!=1||!base||(base&4095)||base>0xffff0000ull||
    (!clear&&phase)||(clear&&(phase!=1||owned_base!=base)))return false;
 // Claim once before any possible store. Failed partial preparation is
 // never reusable; caller retains DSP/memory ownership until reset.
 if(!clear){phase=3;owned_base=base;unsigned x=0;
  if(PE_parse_boot_argn("zizhan_tone_x",&x,sizeof x)){zizhan_audio_tone::tone_shift=x==2?1u:x==4?2u:x==8?3u:0u;}}
 for(unsigned i=0;i<16;++i)if(pmap_valid_page((ppnum_t)((base>>12)+i)))return false;
 auto memory=IODeviceMemory::withRange(base,65536);
 auto map=memory?memory->map(kIOMapAnywhere|kIOMapWriteCombineCache):nullptr;
 if(memory){memory->release();}if(!map)return false;
 auto words=reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress());
 zizhan_audio_tone::Receipt r;
 const bool ok=zizhan_audio_tone::buffer(base,65536,clear,[](uint64_t p){return pmap_valid_page((ppnum_t)p)!=0;},
  [&](unsigned at,uint32_t *v){*v=words[at/4];
#if defined(__aarch64__) && defined(KERNEL)
   __asm__ volatile("dmb oshld":::"memory");
#endif
   return true;},[&](unsigned at,uint32_t v){words[at/4]=v;
#if defined(__aarch64__) && defined(KERNEL)
   __asm__ volatile("dsb oshst":::"memory");
#endif
   return true;},tone_us,r);
#if defined(__aarch64__) && defined(KERNEL)
 __asm__ volatile("dsb sy":::"memory");
#endif
 map->release();if(ok)phase=clear?2u:1u;
 IOLog("ZIZHAN_AUDIO_TONE_BUFFER: %s clear=%u reason=%u pages=%u reads=%u writes=%u verified=%u bytes=11520 frames=960 rate=96000 channels=3 tone_hz=1000 peak_q31=%u taper_frames=96 normal_nc=1 unmanaged=1 output_enables=0\n",ok?"PASS":"FAIL",clear?1u:0u,r.reason,r.pages,r.reads,r.writes,r.verified,33554432u<<zizhan_audio_tone::tone_shift);
 return ok;
}
}
extern "C" bool zizhan_audio_tone_prepare(void){return access(false);}
// Only the same-thread PRM owner calls clear AFTER both actual V2DONEs and
// amp reset, while the MAP lease still exists. Never on uncertain timeout.
extern "C" bool zizhan_audio_tone_clear(void){return access(true);}
