/* Host-only 48kHz/2ch/24-bit-packed test pattern controls; no hardware. */
#include <assert.h>
#include <stdio.h>
#include "ZizhanAudioTone48.h"
int main(void){
 assert(ZizhanTone48Bytes==2880&&ZizhanTone48Stride==0x3000&&ZizhanTone48Slots==4);
 assert(zizhan_tone48_frame(0)==0&&zizhan_tone48_frame(479)==0);
 assert(zizhan_tone48_frame(12)==32768&&zizhan_tone48_frame(60)==131072);
 assert(zizhan_tone48_frame(84)==-131072);
 for(unsigned frame=0;frame<ZizhanTone48Frames;++frame)assert(zizhan_tone48_frame(frame)<=131072&&zizhan_tone48_frame(frame)>=-131072);
 /* Peak frame 12: L/R signed24 0x020000, both channels identical. */
 const unsigned peak=60*6;
 assert(zizhan_tone48_byte(peak+0)==0&&zizhan_tone48_byte(peak+1)==0&&zizhan_tone48_byte(peak+2)==2);
 assert(zizhan_tone48_byte(peak+3)==0&&zizhan_tone48_byte(peak+4)==0&&zizhan_tone48_byte(peak+5)==2);
 for(unsigned slot=0;slot<4;++slot){
  assert(zizhan_tone48_word(slot*ZizhanTone48Stride)==zizhan_tone48_word(0));
  assert(zizhan_tone48_byte(slot*ZizhanTone48Stride+ZizhanTone48Bytes)==0);
  assert(zizhan_tone48_byte(slot*ZizhanTone48Stride+ZizhanTone48Stride-1)==0);
 }
 assert(zizhan_tone48_byte(4*ZizhanTone48Stride)==0);
 assert(zizhan_tone48_word(ZizhanTone48Extent-4)==0);
 unsigned nonzero=0;for(unsigned at=0;at<ZizhanTone48Bytes;++at)if(zizhan_tone48_byte(at))++nonzero;
 assert(nonzero>1000);
 puts("PASS finite 48kHz/2ch/24-bit-packed 1kHz pattern: 480 frames/2880 bytes/four 0x3000 slots, taper and slot isolation; no hardware");
}
