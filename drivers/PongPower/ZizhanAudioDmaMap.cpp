/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include "ZizhanAudioDmaMap.h"
#include "../PongAdreno/ArmSmmu4K/ArmSmmu4KAudit.h"
extern "C" bool pong_smmu_lease_lock(void);
extern "C" void pong_smmu_lease_unlock(void);
extern "C" int pong_klog_flush(int reason) __attribute__((weak));
namespace {
struct Owner {
 struct Write {size_t at;uint64_t value;unsigned bits;};Write trace[32]={};unsigned trace_count=0,trace_overflow=0;
 IOBufferMemoryDescriptor *tables=nullptr;IOMemoryMap *map=nullptr;volatile uint8_t *registers=nullptr;
 uint64_t base=0,bytes=0,table_pa=0;unsigned once=0,lock_held=0;
 ArmSmmu4KConfig lower={};ArmSmmu4KResult built={};ArmSmmu4KContextConfig config={};
 ArmSmmu4KRange range={};unsigned char visited[16]={};ArmSmmu4KAuditRegion audit_region={};ArmSmmu4KAuditConfig audit={};ArmSmmu4KAuditResult audited={};
 ArmSmmu4KIo io={};zizhan_audio_dma_map::State state;
 volatile unsigned watch_stop=0,watch_running=0;
 uint64_t foreign_roots[78][2]={};unsigned namespace_reads=0,active_banks=0;
};
Owner owner;
int read32(void *p,size_t at,uint32_t *value){auto &o=*static_cast<Owner *>(p);if(at>=0x100000||(at&3))return 1;*value=*reinterpret_cast<volatile uint32_t *>(o.registers+at);
#if defined(__aarch64__) && defined(KERNEL)
 __asm__ volatile("dmb oshld":::"memory");
#endif
 return 0;}
int read64(void *p,size_t at,uint64_t *value){auto &o=*static_cast<Owner *>(p);if(at>0x100000-8||(at&7))return 1;*value=*reinterpret_cast<volatile uint64_t *>(o.registers+at);
#if defined(__aarch64__) && defined(KERNEL)
 __asm__ volatile("dmb oshld":::"memory");
#endif
 return 0;}
int barrier(void *){
#if defined(__aarch64__) && defined(KERNEL)
 __asm__ volatile("dsb sy":::"memory");
#endif
 return 0;}
void trace(Owner &o,size_t at,uint64_t value,unsigned bits){
 if(o.trace_count<32){
  const unsigned index=o.trace_count;o.trace[o.trace_count++]={at,value,bits};
  // The rejected control panics inside the restore writer, so the
  // failure-only dump cannot locate the register. Persist each bounded
  // restore attempt BEFORE MMIO; preserve genuine exception handling.
  if(o.state.phase>=4)IOLog("ZIZHAN_AUDIO_DMAMAP_RESTORE: index=%u phase=%u at=%lx bits=%u value=%llx before_write=1\n",index,o.state.phase,static_cast<unsigned long>(at),bits,value);
 }else ++o.trace_overflow;
}
int write32(void *p,size_t at,uint32_t value){auto &o=*static_cast<Owner *>(p);if(at>=0x100000||(at&3))return 1;trace(o,at,value,32);*reinterpret_cast<volatile uint32_t *>(o.registers+at)=value;return barrier(p);}
int write64(void *p,size_t at,uint64_t value){auto &o=*static_cast<Owner *>(p);if(at>0x100000-8||(at&7))return 1;trace(o,at,value,64);*reinterpret_cast<volatile uint64_t *>(o.registers+at)=value;return barrier(p);}
void clean(void *p,size_t bytes){
#if defined(__aarch64__) && defined(KERNEL)
 for(uintptr_t at=reinterpret_cast<uintptr_t>(p)&~uintptr_t(63),end=reinterpret_cast<uintptr_t>(p)+bytes;at<end;at+=64)__asm__ volatile("dc civac,%0"::"r"(at):"memory");
#else
 (void)p;(void)bytes;
#endif
 (void)barrier(nullptr);
}
void unlock(){if(owner.lock_held){pong_smmu_lease_unlock();owner.lock_held=0;}}
void release_safe(){if(owner.map){owner.map->release();owner.map=nullptr;owner.registers=nullptr;}if(owner.tables){owner.tables->release();owner.tables=nullptr;}}
void image_log(const char *kind,const ArmSmmu4KContextImage &v){
 IOLog("ZIZHAN_AUDIO_DMAMAP_IMAGE: kind=%s cbar=%x cba2r=%x tcr=%x tcr2=%x ttbr0=%llx ttbr1=%llx mair0=%x mair1=%x sctlr=%x reserved=%x\n",
  kind,v.cbar,v.cba2r,v.tcr,v.tcr2,v.ttbr0,v.ttbr1,v.mair0,v.mair1,v.sctlr,v.reserved_zero);
}
bool exclusive_asid(const zizhan_audio_dma::Snapshot &capture){
 auto &o=owner;
 // ASIDPNE is explicitly off on the observed apps-SMMU. Do not share our
 // ASID with another active context. Actual global owner remains held.
 for(unsigned pass=0;pass<2;++pass)for(unsigned bank=0;bank<capture.banks;++bank){
  if(bank==capture.plan.bank)continue;
  const unsigned cb=(capture.pages+bank)*4096;uint32_t control=0;++o.namespace_reads;
  if(read32(&o,cb,&control)||control!=capture.sctlr[bank])return false;
  if(!(control&1))continue;
  if(!pass)++o.active_banks;
  for(unsigned root=0;root<2;++root){uint64_t value=0;++o.namespace_reads;
   if(read64(&o,cb+0x20+root*8,&value)||(value>>48)==0x1801)return false;
   if(pass&&value!=o.foreign_roots[bank][root])return false;
   o.foreign_roots[bank][root]=value;
  }
  ++o.namespace_reads;if(read32(&o,cb,&control)||control!=capture.sctlr[bank])return false;
 }
 IOLog("ZIZHAN_AUDIO_DMAMAP: ASID namespace=1 asid=1801 active_banks=%u reads=%u stable=1 no_conflict=1 shared_asid=1\n",o.active_banks,o.namespace_reads);
 return true;
}
bool failed(const char *stage){
 IOLog("ZIZHAN_AUDIO_DMAMAP: FAIL stage=%s phase=%u attempted=%u installed=%u restored=%u uncertain=%u program_status=%u program_phase=%u retained=%u data=0 audio_output=0\n",stage,
  owner.state.phase,owner.state.attempted,owner.state.installed,owner.state.restored,owner.state.uncertain,owner.state.program_status,owner.state.programmed.phase,owner.state.attempted?1u:0u);
 if(owner.state.attempted){
  // Preserve exact pre-recovery observations; do not normalize hardware
  // bits or weaken the shared native4K comparator on an unexplained failure.
  ArmSmmu4KContextImage expected={};const auto encoded=ArmSmmu4KEncodeContext(&owner.config,&expected);
  image_log("prior",owner.state.old);image_log("expected",expected);image_log("program-readback",owner.state.programmed.readback);
  const auto &r=owner.state.programmed;
  IOLog("ZIZHAN_AUDIO_DMAMAP_DIAG: encoded=%u id0=%x id1=%x id2=%x cb=%llx writes=%u polls=%u fault=%x readback_valid=%u enabled=%u disabled=%u tlb=%u retain=%u trace=%u overflow=%u strict_policy=1\n",
   unsigned(encoded),r.id0,r.id1,r.id2,r.context_offset,r.writes,r.poll_reads,r.fault_status,r.readback_valid,r.enabled,r.disabled_verified,r.tlb_synchronized,r.must_retain_tables,owner.trace_count,owner.trace_overflow);
  for(unsigned i=0;i<owner.trace_count;++i){const auto &w=owner.trace[i];IOLog("ZIZHAN_AUDIO_DMAMAP_WRITE: index=%u at=%lx bits=%u value=%llx attempted=1\n",i,static_cast<unsigned long>(w.at),w.bits,w.value);}
 }
 // A partially programmed context may still refer to these real tables.
 // Retain all storage/MMIO until actual reset,without blocking other actors
 // on the global lock. No new audio trial is allowed in this boot.
 if(!owner.state.attempted){release_safe();}unlock();return false;
}
}
namespace {
// zizhan_dma_watch=1 (diagnostics): poll the owned context FSR/FAR/FSYNR0 and global GFSR every ~1 ms while
// the map is installed; log every change and copy the console to the UFS klog (reason 7) so a later
// hang still shows any SMMU fault that preceded it. Read-only MMIO; stopped before the map is released.
void dma_watch(void *,wait_result_t){
 auto &o=owner;const unsigned cb=(o.state.pages+o.state.bank)*4096;uint32_t last_fsr=~0u,last_g=~0u;unsigned polls=0;
 for(unsigned ms=0;ms<120000&&!o.watch_stop;++ms){
  uint32_t fsr=0,g=0,syn=0;uint64_t far=0;(void)read32(&o,cb+0x58,&fsr);(void)read32(&o,0x48,&g);++polls;
  if(fsr!=last_fsr||g!=last_g){(void)read64(&o,cb+0x60,&far);(void)read32(&o,cb+0x68,&syn);
   IOLog("ZIZHAN_AUDIO_DMAWATCH: t_ms=%u fsr=%x far=%llx fsynr0=%x gfsr=%x\n",ms,fsr,far,syn,g);last_fsr=fsr;last_g=g;
   if(ms&&pong_klog_flush)(void)pong_klog_flush(7);}
  IOSleep(1);
 }
 IOLog("ZIZHAN_AUDIO_DMAWATCH: end polls=%u fsr=%x gfsr=%x\n",polls,last_fsr,last_g);
 __atomic_store_n(&o.watch_running,0u,__ATOMIC_RELEASE);
}
void dma_watch_stop(){auto &o=owner;o.watch_stop=1;for(unsigned i=0;i<200&&__atomic_load_n(&o.watch_running,__ATOMIC_ACQUIRE);++i)IOSleep(2);}
}
extern "C" bool zizhan_audio_dma_map_begin(void){
 auto &o=owner;if(o.once++)return false;
 auto chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);auto data=chosen?OSDynamicCast(OSData,chosen->getProperty("pong-audio-dma")):nullptr;
 uint64_t extent[2]={};if(data&&data->getLength()==sizeof extent)memcpy(extent,data->getBytesNoCopy(),sizeof extent);if(chosen)chosen->release();
 o.base=extent[0];o.bytes=extent[1];if(!o.base||(o.base&4095)||o.bytes!=65536||o.base>0x100000000ull-o.bytes)return failed("UEFI-extent");
 if(!pong_smmu_lease_lock()){return failed("global-owner");}o.lock_held=1;
 auto memory=IODeviceMemory::withRange(0x15000000,0x100000);o.map=memory?memory->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;if(memory)memory->release();
 if(!o.map){return failed("MMIO-map");}o.registers=reinterpret_cast<volatile uint8_t *>(o.map->getVirtualAddress());
 auto capture=zizhan_audio_dma::inspect(o.base,o.bytes,[&](unsigned at){uint32_t value=0;(void)read32(&o,at,&value);return value;});
 if(capture.status!=zizhan_audio_dma::Pass||capture.plan.error||capture.plan.reuse||capture.plan.migrate)return failed("fresh-exclusive-plan");
 if(!exclusive_asid(capture))return failed("ASID-namespace");
 o.tables=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,IOOptionBits(kIODirectionInOut)|IOOptionBits(kIOMemoryPhysicallyContiguous)|IOOptionBits(kIOMemoryMapperNone),65536,4096);
 if(!o.tables){return failed("table-allocation");}IOByteCount span=0;o.table_pa=o.tables->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
 if(!o.table_pa||(o.table_pa&4095)||span<65536||o.table_pa>0x1000000000ull-65536||!(o.table_pa+65536<=o.base||o.base+o.bytes<=o.table_pa))return failed("table-extent");
 o.lower={32,36,o.table_pa,16};o.range={o.base,o.base,o.bytes,ArmSmmu4KRead|ArmSmmu4KWrite,0,2};
 if(ArmSmmu4KBuild(&o.lower,&o.range,1,static_cast<uint64_t *>(o.tables->getBytesNoCopy()),16,&o.built)!=ArmSmmu4KSuccess)return failed("native4K-tables");
 o.config.lower=&o.lower;o.config.lower_tables=&o.built;o.config.id0=capture.id[0];o.config.id2=capture.id[2];o.config.asid=0x1801;o.config.coherent_walk=1;o.config.polled_faults=1;
 // Actual apps-SMMU mode26 diagnostic: TCR2 0x38011 read back 0x38071.
 // Explicitly request the existing native4K TBI policy. The encoder admits
 // it only for this lower32-only context; our caller still accepts exactly
 // the real low4GiB UEFI extent. Keep every readback comparison exact.
 o.config.ignore_top_byte=1;
 // Independent actual enable readback: requested0x1027 returned0x27.
 // Request the shared-ASID policy explicitly after the collision guard;
 // no ASIDPNE/readback mask or foreign context change is permitted.
 o.config.shared_asid=1;
 o.audit_region={&o.range,1,16,8192,o.visited,sizeof o.visited};o.audit.lower=&o.audit_region;
 o.audit.io={&o,[](void *p,uint64_t physical,uint64_t *value){auto &v=*static_cast<Owner *>(p);
  if(physical<v.table_pa||(physical&7)||physical-v.table_pa>65536-8)return 1;
  *value=*reinterpret_cast<volatile uint64_t *>(static_cast<uint8_t *>(v.tables->getBytesNoCopy())+physical-v.table_pa);return 0;}};
 if(ArmSmmu4KAuditDeclaredContext(&o.config,&o.audit,&o.audited)!=ArmSmmu4KSuccess)return failed("physical-table-audit");
 clean(o.tables->getBytesNoCopy(),o.built.table_pages*4096);
 o.io={&o,0x100000,read32,read64,write32,write64,barrier,1};
 if(!zizhan_audio_dma_map::install(o.io,capture,o.config,o.state))return failed("program-or-stream");
 if(ArmSmmu4KObserveAuditedContext(&o.io,o.state.bank,&o.config,&o.audit,&o.audited)!=ArmSmmu4KSuccess)return failed("live-table-audit");
 IOLog("ZIZHAN_AUDIO_DMAMAP: audit complete=%u leaves=%lu descriptors=%lu real_owned_tables=1\n",o.audited.complete,static_cast<unsigned long>(o.audited.lower.leaf_pages),static_cast<unsigned long>(o.audited.lower.descriptor_reads));
 IOLog("ZIZHAN_AUDIO_DMAMAP: PASS installed=1 base=%llx bytes=%llu table=%llx pages=%lu slot=%u bank=%u context_readback=%u tlb=%u fault=%x fault_mask=c00001fe masked_fault=%x data=0 audio_output=0\n",
  o.base,o.bytes,o.table_pa,static_cast<unsigned long>(o.built.table_pages),o.state.slot,o.state.bank,o.state.programmed.readback_valid,o.state.programmed.tlb_synchronized,o.state.programmed.fault_status,o.state.programmed.fault_status&0xc00001feu);
 {unsigned watch=0;thread_t thread=nullptr;
  if(PE_parse_boot_argn("zizhan_dma_watch",&watch,sizeof watch)&&watch==1){o.watch_running=1;
   if(kernel_thread_start(dma_watch,nullptr,&thread)==KERN_SUCCESS)thread_deallocate(thread);else o.watch_running=0;}}
 return true;
}
extern "C" bool zizhan_audio_dma_map_end(void){
 auto &o=owner;if(!o.lock_held||!o.state.installed)return false;
 dma_watch_stop();
 if(!zizhan_audio_dma_map::restore(o.io,o.config,o.state))return failed("restore");
 IOLog("ZIZHAN_AUDIO_DMAMAP: PASS installed=0 stream_restored=1 context_retired=1 prior_context_restored=0 uncertain=0 slot=%u bank=%u tlb=%u disabled=%u native_idle_readback=1 data=0 audio_output=0\n",
  o.state.slot,o.state.bank,o.state.disabled.tlb_synchronized,o.state.disabled.disabled_verified);
 release_safe();unlock();return true;
}

extern "C" uint64_t zizhan_audio_dma_map_address(void){return owner.state.installed&&owner.lock_held?(owner.base|(1ull<<32)):0;}
extern "C" void zizhan_audio_dma_map_retain(void){if(owner.state.attempted&&!owner.state.restored){IOLog("ZIZHAN_AUDIO_DMAMAP: RETAIN until-reset phase=%u installed=%u tables=1 audio_output=0\n",owner.state.phase,owner.state.installed);unlock();}}
