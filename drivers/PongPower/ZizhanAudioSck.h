/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_SCK_H
#define ZIZHAN_AUDIO_SCK_H
#include <stdint.h>
namespace zizhan_audio_sck {
struct State {uint32_t config=0,io=0;bool claimed=false,connected=false,restored=false;};
/* Saved live DT121:mi2s2_sck,6mA,biasdisable/outputhigh;cape ALT1.
 * Exact qualified sleep baseline1/inputlow. Both amps must already have
 * read-back GLOBAL/AMP/BST off;the caller retains the actual clock lease. */
template<typename Pin> bool connect_native(Pin &pin,State &s,uint32_t config){
 if(config!=0x284&&config!=0x2c4)return false;
 s.config=pin.read(0);s.io=pin.read(4);
 if(s.config!=1||(s.io&3)||pin.read(0)!=s.config||pin.read(4)!=s.io)return false;
 s.claimed=true;pin.write(4,2);pin.write(0,config);
 s.connected=pin.read(0)==config&&(pin.read(4)&2)==2;return s.connected;
}
template<typename Pin> bool connect(Pin &pin,State &s){return connect_native(pin,s,0x284);}
/* Saved live DT123:mi2s2_ws,8mA,biasdisable/outputhigh,ALT1. */
template<typename Pin> bool connect_ws(Pin &pin,State &s){return connect_native(pin,s,0x2c4);}
template<typename Pin> bool restore(Pin &pin,State &s){
 if(!s.claimed){s.restored=true;return true;}
 // Isolate as GPIO input BEFORE restoring output latch. Never remove a
 // live clock from a running amplifier:caller asserts shared reset first.
 pin.write(0,s.config);pin.write(4,s.io);
 s.restored=pin.read(0)==s.config&&(pin.read(4)&2)==(s.io&2);
 if(s.restored){s.connected=false;}return s.restored;
}
}
#endif
