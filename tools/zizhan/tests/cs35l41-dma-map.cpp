/* Actual shared SMMU programmer/restore transaction;synthetic MMIO only. */
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
#include "ZizhanAudioDmaMap.h"
struct Fake {
 std::array<uint32_t,0x100000/4> r={};unsigned writes=0,fail=0;bool stuck=false,retiring=false;
 Fake(){r[0x20/4]=0x4c017e71;r[0x24/4]=0x6000004b;r[0x28/4]=0x5111;for(unsigned i=0;i<113;++i)r[(0xc00+i*4)/4]=0x20000;}
 ArmSmmu4KIo io(){ArmSmmu4KIo v={};v.cookie=this;v.register_bytes=0x100000;v.polled_faults=1;
  v.read32=[](void *p,size_t at,uint32_t *out){auto &f=*(Fake*)p;assert(at<0x100000&&!(at&3));*out=(f.stuck&&(at&4095)==0x7f4)?1:f.r[at/4];return 0;};
  v.read64=[](void *p,size_t at,uint64_t *out){auto &f=*(Fake*)p;assert(at+8<=0x100000&&!(at&7));memcpy(out,&f.r[at/4],8);return 0;};
  v.write32=[](void *p,size_t at,uint32_t x){auto &f=*(Fake*)p;assert(at<0x100000&&!(at&3));if(f.retiring)assert(at<0x1000||!(at&4095)||(at&4095)==0x610||(at&4095)==0x7f0);if(++f.writes==f.fail)return 1;if((at&4095)==0x58)f.r[at/4]&=~x;else f.r[at/4]=x;return 0;};
  v.write64=[](void *p,size_t at,uint64_t x){auto &f=*(Fake*)p;assert(at+8<=0x100000&&!(at&7)&&!f.retiring);if(++f.writes==f.fail)return 1;memcpy(&f.r[at/4],&x,8);return 0;};
  v.barrier=[](void*){return 0;};return v;
 }
};
int main(){
 alignas(4096) uint64_t tables[8192]={};ArmSmmu4KConfig cfg={32,36,0x700000000,16};
 ArmSmmu4KRange range={0xf0000000,0xf0000000,65536,ArmSmmu4KRead|ArmSmmu4KWrite,0,2};ArmSmmu4KResult built={};
 assert(ArmSmmu4KBuild(&cfg,&range,1,tables,16,&built)==ArmSmmu4KSuccess);
 ArmSmmu4KContextConfig c={};c.lower=&cfg;c.lower_tables=&built;c.asid=0x1801;c.coherent_walk=1;c.polled_faults=1;
 unsigned installed_writes=0,total_writes=0;
 for(unsigned test=0;test<4;++test){Fake f;auto snap=zizhan_audio_dma::inspect(range.physical,range.bytes,[&](unsigned at){return f.r[at/4];});assert(!snap.plan.error);
  const unsigned cb=(snap.pages+snap.plan.bank)*4096;f.r[cb/4]=snap.sctlr[snap.plan.bank]=0x2000230a;
  if(test==1){f.r[(0x800+snap.plan.slot*4)/4]^=1;}if(test==2)f.r[(cb+0x58)/4]=2;if(test==3)f.stuck=true;
  zizhan_audio_dma_map::State state;auto io=f.io();bool ok=zizhan_audio_dma_map::install(io,snap,c,state);
  if(!test){if(!ok)fprintf(stderr,"install fail phase%u attempted%u program%u pphase%u writes%u\n",state.phase,state.attempted,state.program_status,state.programmed.phase,f.writes);assert(ok&&state.installed&&state.uncertain);installed_writes=f.writes;
   f.retiring=true;assert(zizhan_audio_dma_map::restore(io,c,state)&&state.restored&&!state.installed&&!state.uncertain);
   assert(state.retired&&f.r[cb/4]==0&&f.r[(0x1000+state.bank*4)/4]==0x1f300&&f.r[(0x1800+state.bank*4)/4]==1&&f.r[(0x800+snap.plan.slot*4)/4]==0&&f.r[(0xc00+snap.plan.slot*4)/4]==0x20000);total_writes=f.writes;
  }else if(test<=2)assert(!ok&&!state.attempted&&!f.writes);else assert(!ok&&state.attempted&&state.uncertain);
 }
 for(unsigned fail=1;fail<=total_writes;++fail){Fake f;auto snap=zizhan_audio_dma::inspect(range.physical,range.bytes,[&](unsigned at){return f.r[at/4];});f.fail=fail;
  zizhan_audio_dma_map::State state;auto io=f.io();bool ok=zizhan_audio_dma_map::install(io,snap,c,state);
  if(fail<=installed_writes){assert(!ok&&state.uncertain);}
  else{assert(ok);f.retiring=true;assert(!zizhan_audio_dma_map::restore(io,c,state)&&state.uncertain&&!state.restored);}
 }
 for(unsigned changed=0;changed<5;++changed){Fake f;auto snap=zizhan_audio_dma::inspect(range.physical,range.bytes,[&](unsigned at){return f.r[at/4];});zizhan_audio_dma_map::State state;auto io=f.io();
  assert(zizhan_audio_dma_map::install(io,snap,c,state));const unsigned before=f.writes,cb=(snap.pages+snap.plan.bank)*4096;
  if(changed==0)f.r[(0x800+state.slot*4)/4]^=1;
  if(changed==1)f.r[(0xc00+state.slot*4)/4]^=1;
  if(changed==2)f.r[(cb+0x20)/4]^=4096;
  if(changed==3)f.r[cb/4]^=0x100;
  if(changed==4)f.r[(cb+0x58)/4]=2;
  assert(!zizhan_audio_dma_map::restore(io,c,state)&&f.writes==before&&state.uncertain&&!state.restored);
 }
 puts("PASS actual native4K program/exactstream/retire-disabled-native/noCBARorTTBRwrites/restoreunusedstream/fullreadback/TLB/faultorownershiprefusal/allwritefailures retained;no hardware");
}
