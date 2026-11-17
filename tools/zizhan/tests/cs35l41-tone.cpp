/* Tests actual finite waveform/data-buffer core;no hardware. */
#include <cassert>
#include <array>
#include <cstdio>
#include <cstring>
#include "ZizhanAudioTone.h"
int main(int argc,char **argv){
 using namespace zizhan_audio_tone;std::array<uint32_t,Extent/4> data={};
 if(argc==2){for(unsigned i=0;i<Bytes/4;++i)data[i]=sample(i);FILE *f=fopen(argv[1],"wb");assert(f);assert(fwrite(data.data(),1,Bytes,f)==Bytes);assert(!fclose(f));return 0;}
 for(unsigned test=0;test<10;++test){data.fill(0);Receipt r;unsigned stores=0,reads=0;uint64_t time=0;
  if(test==1)data.back()=1;
  const uint64_t base=test==2?0xf0000001:0xf0000000;
  bool pass=buffer(base,Extent,false,[&](uint64_t p){return test==3&&p==0xf0008;},
   [&](unsigned at,uint32_t *v){++reads;*v=data[at/4];if(test==4&&reads==Extent/4+1)*v^=1;return !(test==5&&reads==17);},
   [&](unsigned at,uint32_t v){++stores;if(test==6&&stores==10)return false;data[at/4]=v;return true;},
   [&](){time+=test==7?1000u:1u;return time;},r);
  if(test>=1&&test<=7){assert(!pass&&r.reason);if(test<=3||test==5||test==7)assert(!stores);continue;}
  assert(pass&&r.verified&&r.writes==Bytes/4&&r.reads==Extent/2&&r.pages==16);
  for(unsigned i=Bytes/4;i<Extent/4;++i)assert(!data[i]);
  if(test==8)data[10]^=1;
  Receipt c;unsigned cleared=0;
  bool done=buffer(base,Extent,true,[](uint64_t){return false;},[&](unsigned at,uint32_t *v){*v=data[at/4];return true;},
   [&](unsigned at,uint32_t v){++cleared;data[at/4]=v;return true;},[&](){return ++time;},c);
  if(test==8)assert(!done&&!cleared&&c.reason==3);
  else{assert(done&&c.verified&&cleared==Bytes/4);for(uint32_t v:data)assert(!v);}
 }
 puts("PASS finite10ms waveform/buffer exactpreimage/fullreadback/tailguard/managed/writefail/timeouts/clear-after-match;no hardware");
}
