/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <mach/vm_param.h>
#include "ZizhanAudioDmaMap.h"
#include "ZizhanAudioZero.h"
extern "C" boolean_t pmap_valid_page(ppnum_t);
extern "C" bool zizhan_audio_zero_verify(void){
 const uint64_t tagged=zizhan_audio_dma_map_address(),base=uint32_t(tagged);
 if((tagged>>32)!=1||!base||(base&4095)||base>0xffff0000ull)return false;
 // The firmware reserved this extent, zeroed and flushed it. Refuse any
 // managed VM page instead of creating a differently cached CPU alias.
 for(unsigned i=0;i<16;++i)if(pmap_valid_page((ppnum_t)((base>>12)+i))){IOLog("ZIZHAN_AUDIO_ZERO: FAIL managed_page=%u reads=0 writes=0\n",i);return false;}
 auto memory=IODeviceMemory::withRange(base,65536);
 auto map=memory?memory->map(kIOMapAnywhere|kIOMapWriteCombineCache|kIOMapReadOnly):nullptr;
 if(memory){memory->release();}if(!map){IOLog("ZIZHAN_AUDIO_ZERO: FAIL CPU-map reads=0 writes=0\n");return false;}
 const auto words=reinterpret_cast<volatile uint64_t *>(map->getVirtualAddress());
 zizhan_audio_zero::Receipt r;
 const bool ok=zizhan_audio_zero::verify(base,65536,[](uint64_t page){return pmap_valid_page((ppnum_t)page)!=0;},
  [&](unsigned at,uint64_t *value){*value=words[at/8];
#if defined(__aarch64__) && defined(KERNEL)
   __asm__ volatile("dmb oshld":::"memory");
#endif
   return true;},r);
#if defined(__aarch64__) && defined(KERNEL)
 __asm__ volatile("dsb sy":::"memory");
#endif
 map->release();
 IOLog("ZIZHAN_AUDIO_ZERO: %s reason=%u pages=%u reads=%u passes=%u bytes=65536 readonly=1 normal_nc=1 unmanaged=1 writes=0 audio_output=0\n",ok?"PASS":"FAIL",r.reason,r.pages,r.reads,r.passes);
 return ok;
}
