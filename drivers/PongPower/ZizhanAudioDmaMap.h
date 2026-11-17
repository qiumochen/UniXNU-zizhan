/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_DMA_MAP_H
#define ZIZHAN_AUDIO_DMA_MAP_H
#include "ZizhanAudioDma.h"
#include "../PongAdreno/ArmSmmu4K/ArmSmmu4KProgram.h"
namespace zizhan_audio_dma_map {
/* A new exact stream only,never a firmware migration or context reuse.
 * Caller owns real tables/global SMMU lock and quiescent SID1801. */
struct State {
 // restored means the prior unused stream slot is restored. The context
 // is retired disabled in its qualified native format, not raw-restored.
 unsigned attempted=0,installed=0,restored=0,retired=0,uncertain=0,phase=0;
 unsigned bank=0,slot=0,pages=0,program_status=0,disable_status=0;uint32_t old_smr=0,old_s2cr=0;
 ArmSmmu4KContextImage old={};ArmSmmu4KProgramResult programmed={},observed={},disabled={};
};
static inline bool read_image(const ArmSmmu4KIo &io,unsigned cb,unsigned bank,ArmSmmu4KContextImage &v){
 return !io.read32(io.cookie,0x1000+bank*4,&v.cbar)&&!io.read32(io.cookie,0x1800+bank*4,&v.cba2r)&&
  !io.read32(io.cookie,cb+0x30,&v.tcr)&&!io.read32(io.cookie,cb+0x10,&v.tcr2)&&
  !io.read64(io.cookie,cb+0x20,&v.ttbr0)&&!io.read64(io.cookie,cb+0x28,&v.ttbr1)&&
  !io.read32(io.cookie,cb+0x38,&v.mair0)&&!io.read32(io.cookie,cb+0x3c,&v.mair1)&&!io.read32(io.cookie,cb,&v.sctlr);
}
static inline bool install(const ArmSmmu4KIo &io,const zizhan_audio_dma::Snapshot &capture,
 const ArmSmmu4KContextConfig &config,State &s){
 if(capture.status!=zizhan_audio_dma::Pass||!capture.stable||capture.plan.error||capture.plan.reuse||capture.plan.migrate||
  capture.plan.slot>=capture.groups||capture.plan.bank>=capture.banks||!config.lower||config.asid!=0x1801)return false;
 s.bank=capture.plan.bank;s.slot=capture.plan.slot;s.pages=capture.pages;const unsigned cb=(s.pages+s.bank)*4096;
 s.phase=1;
 if(!read_image(io,cb,s.bank,s.old)||(s.old.sctlr&1)||
  io.read32(io.cookie,0x800+s.slot*4,&s.old_smr)||io.read32(io.cookie,0xc00+s.slot*4,&s.old_s2cr)||
  s.old_smr!=capture.smr[s.slot]||s.old_s2cr!=capture.s2cr[s.slot]||s.old_smr||s.old_s2cr!=0x20000u||
  s.old.sctlr!=capture.sctlr[s.bank]||s.old.cbar!=capture.cbar[s.bank]||s.old.cba2r!=capture.cba2r[s.bank])return false;
 uint32_t fsr=0;if(io.read32(io.cookie,cb+0x58,&fsr)||(fsr&0xc00001feu))return false;
 s.attempted=1;s.uncertain=1;s.phase=2;
 s.program_status=ArmSmmu4KProgramDisabledContext(&io,s.bank,&config,1024,&s.programmed);if(s.program_status!=ArmSmmu4KSuccess)return false;
 // Publish a fresh stream only AFTER the real context and TLB are ready.
 const bool exids=(capture.id[0]>>8)&1;uint32_t value=0;
 if(io.write32(io.cookie,0xc00+s.slot*4,s.bank)||io.write32(io.cookie,0x800+s.slot*4,(exids?0u:0x80000000u)|0x1801u)||
  (exids&&io.write32(io.cookie,0xc00+s.slot*4,s.bank|0x400u))||io.barrier(io.cookie)||
  io.read32(io.cookie,0x800+s.slot*4,&value)||value!=((exids?0u:0x80000000u)|0x1801u)||
  io.read32(io.cookie,0xc00+s.slot*4,&value)||value!=(s.bank|(exids?0x400u:0u)))return false;
 s.installed=1;s.phase=3;return true;
}
static inline bool restore(const ArmSmmu4KIo &io,const ArmSmmu4KContextConfig &config,State &s){
 if(!s.installed||s.restored){return false;}const unsigned cb=(s.pages+s.bank)*4096;s.phase=4;
 // Revalidate actual ownership before the FIRST restore write. The global
 // host lock does not serialize firmware actors or grant a stale lease.
 uint32_t id0=0,smr=0,s2cr=0;
 if(io.read32(io.cookie,0x20,&id0)||io.read32(io.cookie,0x800+s.slot*4,&smr)||io.read32(io.cookie,0xc00+s.slot*4,&s2cr))return false;
 const bool exids=(id0>>8)&1;
 if(smr!=((exids?0u:0x80000000u)|0x1801u)||s2cr!=(s.bank|(exids?0x400u:0u)))return false;
 s.disable_status=ArmSmmu4KObserveOwnedContext(&io,s.bank,&config,&s.observed);
 if(s.disable_status!=ArmSmmu4KSuccess)return false;
 // All DSP buffers/handles must already be released by the caller. Fault
 // the owned stream first,then disable/synchronize. Do not rewrite prior
 // CBAR0: the actual mode27 trace traps on that write after disable/TLB.
 if(io.write32(io.cookie,0xc00+s.slot*4,0x20000u)||io.barrier(io.cookie))return false;
 s.disable_status=ArmSmmu4KDisableOwnedContext(&io,s.bank,&config,1024,&s.disabled);if(s.disable_status!=ArmSmmu4KSuccess)return false;
 s.phase=5;
 if(io.write32(io.cookie,0x800+s.slot*4,s.old_smr)||
  io.write32(io.cookie,0xc00+s.slot*4,s.old_s2cr)||io.barrier(io.cookie))return false;
 ArmSmmu4KContextImage actual={};smr=0;s2cr=0;
 if(!read_image(io,cb,s.bank,actual)||io.read32(io.cookie,0x800+s.slot*4,&smr)||io.read32(io.cookie,0xc00+s.slot*4,&s2cr)||
  smr!=s.old_smr||s2cr!=s.old_s2cr||actual.sctlr)return false;
 // Exact native configuration remains, with M=0, the original invalid
 // stream restored and completed TLB invalidation. This is the normal
 // disabled-context lifetime: no master can reference the owned tables.
 // A later owner must program its own context before publishing a stream.
 ArmSmmu4KContextImage expected={};ArmSmmu4KContextConfig live=config;
 live.id0=s.observed.id0;live.id2=s.observed.id2;
 if(ArmSmmu4KEncodeContext(&live,&expected)!=ArmSmmu4KSuccess)return false;
 actual.sctlr=expected.sctlr;
 if(ArmSmmu4KValidateContextReadback(&live,&actual)!=ArmSmmu4KSuccess)return false;
 uint32_t fault=0;if(io.read32(io.cookie,cb+0x58,&fault)||(fault&0xc00001feu)||io.barrier(io.cookie))return false;
 s.restored=1;s.retired=1;s.installed=0;s.uncertain=0;s.phase=6;return true;
}
}
extern "C" bool zizhan_audio_dma_map_begin(void);
extern "C" bool zizhan_audio_dma_map_end(void);
extern "C" uint64_t zizhan_audio_dma_map_address(void);
extern "C" void zizhan_audio_dma_map_retain(void);
#endif
