/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_DATA_PIN_H
#define ZIZHAN_AUDIO_DATA_PIN_H
#include <stdint.h>
namespace zizhan_audio_data_pin {
struct State {uint32_t config=0,io=0;bool claimed=false,connected=false,restored=false;};
/* Actual saved tert_tdm_dout GPIO124: ALT1/8mA/bias-disable; no
 * output-high or GPIO latch write. Caller verifies both amps off and
 * holds native TDM/PRM clocks. GPIO122(data0) is not touched. */
template<class Pin> bool connect(Pin &pin,State &s){
 s.config=pin.read(0);s.io=pin.read(4);
 if(s.config!=1||(s.io&3)||pin.read(0)!=s.config||pin.read(4)!=s.io)return false;
 s.claimed=true;pin.write(0,0xc4);
 s.connected=pin.read(0)==0xc4&&(pin.read(4)&2)==0;return s.connected;
}
template<class Pin> bool restore(Pin &pin,State &s,bool reset_held){
 if(!s.claimed){s.restored=true;return true;}
 if(!reset_held)return false;
 // Actual shared reset must already be asserted by the caller.
 pin.write(0,s.config);s.restored=pin.read(0)==s.config&&(pin.read(4)&2)==(s.io&2);
 if(s.restored){s.connected=false;}return s.restored;
}
}
#endif
