/* Production zero qualification, controlled observations only. */
#include <cassert>
#include <cstdio>
#include "ZizhanAudioZero.h"
int main(){
 for(unsigned test=0;test<22;++test){zizhan_audio_zero::Receipt r;unsigned reads=0,pages=0;
  bool ok=zizhan_audio_zero::verify(test==1?0:test==2?0xf0000001:0xf0000000,test==3?4096:65536,
   [&](uint64_t page){assert(page==0xf0000+pages);const unsigned index=pages++;return test>=6&&index==test-6;},
   [&](unsigned at,uint64_t *value){assert(at==(reads%8192)*8);++reads;*value=test==4&&reads==8193?1:0;return !(test==5&&reads==8192);},r);
  if(!test)assert(ok&&r.pages==16&&r.reads==16384&&r.passes==2&&!r.reason);
  else if(test<=3)assert(!ok&&r.reason==1&&!r.pages&&!r.reads);
  else if(test<=5)assert(!ok&&r.reason==3&&r.pages==16&&r.reads==reads);
  else assert(!ok&&r.reason==2&&r.pages==test-5&&!reads);
 }
 puts("PASS actual reservedzero core/two complete passes/first late nonzero/readfailure/all16 managedpage refusals/no writes;no hardware");
}
