/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_DMA_H
#define ZIZHAN_AUDIO_DMA_H
#include <stdint.h>
namespace zizhan_audio_dma {
/* Reuse the actual shared SMMUv2 read-only planner. Its writer is not
 * called here. Saved zizhan DT bounds:SID1801,78CB,120SMR. */
#include "../NativeUfs/SmmuTakeover.h"
enum {Skip,Pass,Fail};
struct Snapshot {
 unsigned status=Skip,reason=0,reads=0,banks=0,groups=0,pages=0,stable=0;
 uint64_t base=0,bytes=0;uint32_t id[3]={},smr[120]={},s2cr[120]={},sctlr[78]={},cbar[78]={},cba2r[78]={};
 UfsSmmuPlan plan;
 uint32_t operator()(unsigned at)const{
  if(at>=0x20&&at<=0x28)return id[(at-0x20)/4];
  if(at>=0x800&&at<0x800+groups*4)return smr[(at-0x800)/4];
  if(at>=0xc00&&at<0xc00+groups*4)return s2cr[(at-0xc00)/4];
  if(at>=0x1000&&at<0x1000+banks*4)return cbar[(at-0x1000)/4];
  if(at>=0x1800&&at<0x1800+banks*4)return cba2r[(at-0x1800)/4];
  if(!(at&4095)&&at>=pages*4096&&at<(pages+banks)*4096)return sctlr[at/4096-pages];
  return 0xffffffffu;
 }
};
template<class Read> Snapshot inspect(uint64_t base,uint64_t bytes,Read read){
 Snapshot s;s.base=base;s.bytes=bytes;
 // Real UEFI allocation only;never substitute a board carveout/address.
 if(!base||(base&4095)||bytes!=65536||base>0x100000000ull-bytes){s.reason=1;return s;}
 for(unsigned i=0;i<3;++i){s.id[i]=read(0x20+i*4);++s.reads;}
 unsigned hardware_banks=s.id[1]&255,hardware_groups=s.id[0]&255;
 s.pages=1u<<(((s.id[1]>>28)&7)+1);
 if(!(s.id[0]&(1u<<30))||!(s.id[2]&(1u<<12))||(s.id[1]>>31)||!hardware_banks||!hardware_groups||s.pages>128){s.status=Fail;s.reason=2;return s;}
 s.banks=hardware_banks<78?hardware_banks:78;s.groups=hardware_groups<120?hardware_groups:120;
 if((s.pages+s.banks)*4096>0x100000){s.status=Fail;s.reason=2;return s;}
 for(unsigned i=0;i<s.groups;++i){s.smr[i]=read(0x800+i*4);s.s2cr[i]=read(0xc00+i*4);s.reads+=2;}
 for(unsigned i=0;i<s.banks;++i){s.sctlr[i]=read((s.pages+i)*4096);s.cbar[i]=read(0x1000+i*4);s.cba2r[i]=read(0x1800+i*4);s.reads+=3;}
 // Two complete ordered captures under caller's actual SMMU lock. A
 // changed register fails;no live mapping/CB is ever reconfigured here.
 bool stable=true;
 for(unsigned i=0;i<3;++i){if(read(0x20+i*4)!=s.id[i])stable=false;++s.reads;}
 for(unsigned i=0;i<s.groups;++i){if(read(0x800+i*4)!=s.smr[i])stable=false;if(read(0xc00+i*4)!=s.s2cr[i])stable=false;s.reads+=2;}
 for(unsigned i=0;i<s.banks;++i){if(read((s.pages+i)*4096)!=s.sctlr[i])stable=false;if(read(0x1000+i*4)!=s.cbar[i])stable=false;if(read(0x1800+i*4)!=s.cba2r[i])stable=false;s.reads+=3;}
 if(!stable){s.status=Fail;s.reason=3;return s;}
 s.stable=1;s.plan=planUfsSmmu(0x1801,base,bytes,s,78,120);s.status=Pass;return s;
}
}
#endif
