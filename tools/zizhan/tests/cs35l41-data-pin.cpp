#include <cassert>
#include <cstdio>
#include "ZizhanAudioDataPin.h"
struct Pin {uint32_t config=1,io=0;unsigned writes=0,reads=0,fail_write=0,change_read=0;
 uint32_t read(unsigned at){++reads;return at==0?(reads==change_read?config^4:config):io;}
 void write(unsigned at,uint32_t value){assert(at==0);if(++writes!=fail_write)config=value;}
};
int main(){for(unsigned test=0;test<9;++test){Pin p;zizhan_audio_data_pin::State s;
 if(test==1){p.config=4;}if(test==2)p.io=2;if(test==3)p.io=1;if(test==4)p.change_read=3;if(test==5)p.fail_write=1;
 bool ok=zizhan_audio_data_pin::connect(p,s);
 if(test>=1&&test<=4){assert(!ok&&!s.claimed&&!p.writes);continue;}
 if(test==5){assert(!ok&&s.claimed&&!s.connected);assert(zizhan_audio_data_pin::restore(p,s,true)&&s.restored);continue;}
 assert(ok&&s.claimed&&s.connected&&p.config==0xc4&&p.io==0&&p.writes==1);
 if(test==6){assert(!zizhan_audio_data_pin::restore(p,s,false)&&p.writes==1&&!s.restored);continue;}
 if(test==7){p.fail_write=2;}if(test==8)p.io=2;
 bool restored=zizhan_audio_data_pin::restore(p,s,true);assert(restored==(test==0));if(!test)assert(p.config==1&&!p.io&&p.writes==2&&s.restored&&!s.connected);
 }
 puts("PASS actual GPIO124 native ALT1/8mA c4/no latch writes/exact input baseline/partialclaim/restore requires real reset/negative readbacks;no hardware");
}
