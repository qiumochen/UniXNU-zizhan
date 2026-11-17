/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_TONE48_H
#define ZIZHAN_AUDIO_TONE48_H
#include <stdint.h>
#include <stdbool.h>
/* Finite 48kHz/2ch/24-bit-packed test pattern for four 10ms slots.
 * 1kHz, -36dBFS peak, symmetric 1ms taper, both channels identical.
 * The MFC is expected to convert this to the 96kHz/3ch hardware format.
 * No runtime float, streaming, volume control or output enable. */
enum {ZizhanTone48Frames=480u,ZizhanTone48Bytes=2880u,ZizhanTone48Slots=4u,
      ZizhanTone48Extent=65536u,ZizhanTone48Stride=0x3000u};
static const int32_t zizhan_tone48_sine[48]={
 0,17108,33924,50159,65536,79792,92682,103986,113512,121095,126606,129951,
 131072,129951,126606,121095,113512,103986,92682,79792,65536,50159,33924,17108,
 0,-17108,-33924,-50159,-65536,-79792,-92682,-103986,-113512,-121095,-126606,-129951,
 -131072,-129951,-126606,-121095,-113512,-103986,-92682,-79792,-65536,-50159,-33924,-17108};
static inline int32_t zizhan_tone48_frame(unsigned frame){
 const unsigned tail=ZizhanTone48Frames-1-frame;
 unsigned ramp=frame<tail?frame:tail;if(ramp>48)ramp=48;
 return (int32_t)((int64_t)zizhan_tone48_sine[frame%48]*ramp/48);
}
static inline uint8_t zizhan_tone48_byte(unsigned offset){
 const unsigned slot=offset/ZizhanTone48Stride,within=offset%ZizhanTone48Stride;
 if(slot>=ZizhanTone48Slots||within>=ZizhanTone48Bytes)return 0;
 const unsigned frame=within/6,byte=within%3;
 const int32_t value=zizhan_tone48_frame(frame);
 return (uint8_t)(value>>(8*byte));
}
static inline uint32_t zizhan_tone48_word(unsigned offset){
 return (uint32_t)zizhan_tone48_byte(offset)|(uint32_t)zizhan_tone48_byte(offset+1)<<8|
        (uint32_t)zizhan_tone48_byte(offset+2)<<16|(uint32_t)zizhan_tone48_byte(offset+3)<<24;
}
#ifdef __cplusplus
extern "C" {
#endif
bool zizhan_audio_tone48_prepare(void);
bool zizhan_audio_tone48_clear(void);
#ifdef __cplusplus
}
#endif
#endif
