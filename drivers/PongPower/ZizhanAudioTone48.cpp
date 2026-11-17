/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <mach/vm_param.h>
#include "ZizhanAudioDmaMap.h"
#include "ZizhanAudioTone48.h"
extern "C" boolean_t pmap_valid_page(ppnum_t);
namespace {
unsigned phase=0;uint64_t owned_base=0;
bool access(bool clear){
 const uint64_t tagged=zizhan_audio_dma_map_address(),base=uint32_t(tagged);
 if((tagged>>32)!=1||!base||(base&4095)||base>0xffff0000ull||
    (!clear&&phase)||(clear&&(phase!=1||owned_base!=base)))return false;
 if(!clear){phase=3;owned_base=base;}
 for(unsigned i=0;i<16;++i)if(pmap_valid_page((ppnum_t)((base>>12)+i)))return false;
 auto memory=IODeviceMemory::withRange(base,65536);
 auto map=memory?memory->map(kIOMapAnywhere|kIOMapWriteCombineCache):nullptr;
 if(memory){memory->release();}if(!map)return false;
 auto words=reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress());
 unsigned reads=0,writes=0,reason=0;
 for(unsigned at=0;at<ZizhanTone48Extent;at+=4){++reads;
  const uint32_t expected=clear?zizhan_tone48_word(at):0u;
  if(words[at/4]!=expected){reason=1;break;}
 }
 if(!reason){
  for(unsigned slot=0;slot<ZizhanTone48Slots;++slot)
   for(unsigned at=slot*ZizhanTone48Stride;at<slot*ZizhanTone48Stride+ZizhanTone48Bytes;at+=4){
    ++writes;words[at/4]=clear?0u:zizhan_tone48_word(at);
   }
 }
#if defined(__aarch64__) && defined(KERNEL)
 __asm__ volatile("dsb sy":::"memory");
#endif
 if(!reason)for(unsigned at=0;at<ZizhanTone48Extent;at+=4){++reads;
  const uint32_t expected=clear?0u:zizhan_tone48_word(at);
  if(words[at/4]!=expected){reason=2;break;}
 }
 map->release();
 if(!reason)phase=clear?2u:1u;
 IOLog("ZIZHAN_AUDIO_TONE48: %s clear=%u reason=%u pages=16 reads=%u writes=%u bytes=11520 frames=480 rate=48000 channels=2 slots=4 output_enables=0\n",
  !reason?"PASS":"FAIL",clear?1u:0u,reason,reads,writes);
 return !reason;
}
}
extern "C" bool zizhan_audio_tone48_prepare(void){return access(false);}
extern "C" bool zizhan_audio_tone48_clear(void){return access(true);}
