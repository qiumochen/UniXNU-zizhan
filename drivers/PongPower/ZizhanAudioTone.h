/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_TONE_H
#define ZIZHAN_AUDIO_TONE_H
#include <stdint.h>
extern "C" bool zizhan_audio_tone_prepare(void);
extern "C" bool zizhan_audio_tone_clear(void);
namespace zizhan_audio_tone {
/* Finite10ms/1kHz,96kHz3ch32storage/24logical/MSB waveform. Channels0/1
 * carry identical -36dBFS peak; channel2 remains zero. Symmetric1ms
 * taper starts/ends at zero. No runtime float,streaming or volume control.
 * This is data preparation only; it has no power/codec/output operation. */
static constexpr uint32_t Bytes=11520,Extent=65536,Frames=960;
/* zizhan_tone_x=2|4|8 (attended diagnostics) scales the peak by 6/12/18 dB; set once before prepare. */
inline unsigned tone_shift=0;
static const int32_t sine[96]={
 0,2194688,4379648,6546176,8684544,10785792,12840704,14840832,16777216,18641920,20426752,22124032,
 23726592,25227520,26620416,27899392,29059072,30094080,31000320,31773696,32411136,32909568,33267456,33482496,
 33554432,33482496,33267456,32909568,32411136,31773696,31000320,30094080,29059072,27899392,26620416,25227520,
 23726592,22124032,20426752,18641920,16777216,14840832,12840704,10785792,8684544,6546176,4379648,2194688,
 0,-2194688,-4379648,-6546176,-8684544,-10785792,-12840704,-14840832,-16777216,-18641920,-20426752,-22124032,
 -23726592,-25227520,-26620416,-27899392,-29059072,-30094080,-31000320,-31773696,-32411136,-32909568,-33267456,-33482496,
 -33554432,-33482496,-33267456,-32909568,-32411136,-31773696,-31000320,-30094080,-29059072,-27899392,-26620416,-25227520,
 -23726592,-22124032,-20426752,-18641920,-16777216,-14840832,-12840704,-10785792,-8684544,-6546176,-4379648,-2194688,
};
static inline uint32_t sample(unsigned word){
 if(word>=Bytes/4||word%3==2)return 0;
 const unsigned frame=word/3,tail=Frames-1-frame;
 unsigned ramp=frame<tail?frame:tail;if(ramp>96)ramp=96;
 const int64_t scaled=(int64_t(sine[frame%96])*(int64_t(1)<<tone_shift))*ramp/96;
 // Signed24 in32 MSB: preserve low8zero even through integer taper.
 return uint32_t((scaled/256)*256);
}
struct Receipt{unsigned pages=0,reads=0,writes=0,verified=0,reason=0;};
template<class Managed,class Read,class Write,class Clock>
bool buffer(uint64_t base,uint64_t bytes,bool clear,Managed managed,Read read,Write write,Clock clock,Receipt &r){
 if(!base||(base&4095)||bytes!=Extent||base>0x100000000ull-bytes){r.reason=1;return false;}
 for(unsigned page=0;page<16;++page){++r.pages;if(managed((base>>12)+page)){r.reason=2;return false;}}
 const uint64_t began=clock();
 // Exact whole extent preimage before any CPU store. A failed read or an
 // unexpected preimage never authorizes zeroing someone else's buffer.
 for(unsigned at=0;at<Extent;at+=4){uint32_t value=0;++r.reads;
  if(clock()-began>=2000000){r.reason=5;return false;}
  if(!read(at,&value)||value!=(clear?sample(at/4):0u)){r.reason=3;return false;}}
 for(unsigned at=0;at<Bytes;at+=4){
  if(clock()-began>=2000000){r.reason=5;return false;}
  ++r.writes;if(!write(at,clear?0u:sample(at/4))){r.reason=4;return false;}}
 for(unsigned at=0;at<Extent;at+=4){uint32_t value=0;++r.reads;
  if(clock()-began>=2000000){r.reason=5;return false;}
  if(!read(at,&value)||value!=(clear?0u:sample(at/4))){r.reason=3;return false;}}
 r.verified=1;return true;
}
}
#endif
