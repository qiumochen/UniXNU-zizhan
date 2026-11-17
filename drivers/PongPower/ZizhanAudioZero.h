/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_ZERO_H
#define ZIZHAN_AUDIO_ZERO_H
#include <stdint.h>
extern "C" bool zizhan_audio_zero_verify(void);
namespace zizhan_audio_zero {
/* DarwinBoot's real reserved16page buffer is zeroed and flushed before
 * XNU entry. Accept only that unmanaged extent and two complete actual
 * zero observations. Caller owns a read-only Normal-NC CPU mapping and
 * prevents DSP access during this check. No CPU buffer writes. */
struct Receipt {unsigned pages=0,reads=0,passes=0,reason=0;};
template<class Managed,class Read> bool verify(uint64_t base,uint64_t bytes,Managed managed,Read read,Receipt &r){
 if(!base||(base&4095)||bytes!=65536||base>0x100000000ull-bytes){r.reason=1;return false;}
 for(unsigned page=0;page<16;++page){++r.pages;if(managed((base>>12)+page)){r.reason=2;return false;}}
 for(unsigned pass=0;pass<2;++pass){
  for(unsigned at=0;at<65536;at+=8){uint64_t value=~uint64_t(0);++r.reads;if(!read(at,&value)||value){r.reason=3;return false;}}
  ++r.passes;
 }
 return true;
}
}
#endif
