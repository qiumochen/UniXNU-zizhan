/* Host-only zizhan audio control page and producer-watermark controls. */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanAudioAbi.h"
static ZizhanAudioControl fresh(void){
 ZizhanAudioControl c={0};c.magic=ZIZHAN_AUDIO_MAGIC;c.version=ZIZHAN_AUDIO_VERSION;
 c.rate=ZIZHAN_AUDIO_RATE;c.out_channels=ZIZHAN_AUDIO_OUT_CHANNELS;c.in_channels=ZIZHAN_AUDIO_IN_CHANNELS;
 c.period_frames=ZIZHAN_AUDIO_PERIOD_FRAMES;c.ring_frames=ZIZHAN_AUDIO_RING_FRAMES;
 c.lead_frames=ZIZHAN_AUDIO_LEAD_FRAMES;c.ticks_per_frame_q32=1;return c;
}
int main(void){
 assert(sizeof(ZizhanAudioControl)==112);
 assert(offsetof(ZizhanAudioControl,out_written)==80);
 assert(offsetof(ZizhanAudioControl,written_seq)==88);
 ZizhanAudioControl c={0};assert(!zizhan_audio_control_valid(&c)&&!zizhan_audio_ready(&c));
 c=fresh();assert(zizhan_audio_control_valid(&c)&&!zizhan_audio_ready(&c));
 c.state=ZizhanAudioOutputReady;assert(zizhan_audio_ready(&c));
 zizhan_audio_publish_written(&c,480);uint64_t w=0;assert(zizhan_audio_written_stable(&c,&w)&&w==480);
 assert(zizhan_audio_has_frames(&c,0,480));
 assert(!zizhan_audio_has_frames(&c,1,480));
 assert(!zizhan_audio_has_frames(&c,UINT64_MAX-100,480));
 c.written_seq=1;assert(!zizhan_audio_written_stable(&c,&w));c.written_seq=2;
 assert(zizhan_audio_written_stable(&c,&w)&&w==480);
 for(unsigned test=0;test<8;++test){
  ZizhanAudioControl b=fresh();
  if(test==0){b.version++;}if(test==1)b.rate=44100;if(test==2)b.out_channels=1;
  if(test==3){b.in_channels=1;}if(test==4)b.ring_frames++;if(test==5)b.lead_frames=0;
  if(test==6){b.lead_frames=b.ring_frames;}if(test==7)b.ticks_per_frame_q32=0;
  assert(!zizhan_audio_control_valid(&b));
 }
 zizhan_audio_publish_written(NULL,1);
 puts("PASS zizhan audio control ABI: 48k/2ch/65536/10ms/lead, tear-free producer watermark, ready and underrun gates; no hardware");
}
