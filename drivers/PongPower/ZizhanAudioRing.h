/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_RING_H
#define ZIZHAN_AUDIO_RING_H
#include <stdint.h>
#include <string.h>
/* The existing system HAL ring is 48kHz stereo int16 interleaved, indexed
 * by CoreAudio sample time, 65536 frames. The original Android speaker
 * output is also 48kHz 2ch but 24-bit packed; the native ADSP subgraph
 * contains a Media Format Converter (MFC 0x07001015) before the 96kHz
 * 3ch TDM sink. This packer performs only the int16->24-bit storage
 * conversion for one 10ms period. It does not resample, duplicate or
 * invent channels, and it never turns missing HAL data into silence. */
enum {ZizhanRingFrames=65536u,ZizhanRingRate=48000u,ZizhanRingChannels=2u,
      ZizhanRingPcmFrames=480u,ZizhanRingPcmBytes=2880u};
typedef struct {unsigned packed,underruns,reason;uint64_t frame;} ZizhanRingReceipt;
static inline void zizhan_ring_put24(uint8_t *p,int32_t value){
 p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);p[2]=(uint8_t)(value>>16);
}
/* The caller must already know that the HAL produced through frame+480.
 * zizhan_ring_ready is the separate underrun gate; a caller that skips it
 * is responsible for proving the same freshness by another mechanism. */
static inline int zizhan_ring_ready(uint64_t produced_frames,uint64_t frame,ZizhanRingReceipt *r){
 if(r){memset(r,0,sizeof *r);r->frame=frame;}
 if(!r)return 0;
 if(produced_frames<frame||produced_frames-frame<ZizhanRingPcmFrames){
  ++r->underruns;r->reason=2;return 0;
 }
 return 1;
}
static inline int zizhan_ring_pack(const int16_t *ring,unsigned ring_frames,uint64_t frame,
                                   uint8_t out[ZizhanRingPcmBytes],ZizhanRingReceipt *r){
 if(!r)return 0;
 memset(r,0,sizeof *r);r->frame=frame;
 if(!ring||ring_frames!=ZizhanRingFrames||!out||frame>UINT64_MAX-ZizhanRingPcmFrames){
  r->reason=1;return 0;
 }
 for(unsigned i=0;i<ZizhanRingPcmFrames;++i){
  const unsigned at=(unsigned)((frame+i)%ZizhanRingFrames);
  zizhan_ring_put24(out+6*i,(int32_t)ring[2*at]*256);
  zizhan_ring_put24(out+6*i+3,(int32_t)ring[2*at+1]*256);
 }
 r->packed=ZizhanRingPcmBytes;return 1;
}
#endif
