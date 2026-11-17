/* Read-only production SMMU inspection,synthetic layouts,no hardware. */
#include <cassert>
#include <cstdio>
#include <array>
#include "ZizhanAudioDma.h"
int main(){
 using namespace zizhan_audio_dma;
 std::array<uint32_t,0x100000/4> regs={};
 regs[0x20/4]=(1u<<30)|(15u<<9)|120;regs[0x24/4]=78;regs[0x28/4]=(1u<<12)|0x22;
 for(unsigned test=0;test<8;++test){
  auto values=regs;unsigned reads=0;uint64_t base=0xf0000000,bytes=65536;
  if(test==1){base=0;}if(test==2)base++;if(test==3)bytes=4096;if(test==4)base=0xffff1000;
  if(test==5){values[0x24/4]=0;}if(test==7){values[0x800/4]=0x80001801;values[0xc00/4]=3;values[(2+3)*4096/4]=1;}
  auto s=inspect(base,bytes,[&](unsigned at){assert(at<0x100000&&!(at&3));++reads;
   if(test==6&&reads>477&&at==0x20){return values[at/4]^1;}return values[at/4];});
  if(!test){assert(s.status==Pass&&s.stable&&s.banks==78&&s.groups==120&&!s.plan.error&&s.plan.slot<120&&s.plan.bank<78&&reads==954);}
  else if(test<=4)assert(s.status==Skip&&!reads);
  else if(test==5)assert(s.status==Fail&&s.reason==2);
  else if(test==6)assert(s.status==Fail&&s.reason==3&&!s.stable);
  else assert(s.status==Pass&&s.stable&&s.plan.migrate&&s.plan.previousBank==3);
 }
 puts("PASS ownedextent/SMMU actualinspector/geometry/fullstable captures/readonlyplan/migration;no mappingwrites/DMA/data/hardware");
}
