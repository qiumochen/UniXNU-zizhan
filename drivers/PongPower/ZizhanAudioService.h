/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_SERVICE_H
#define ZIZHAN_AUDIO_SERVICE_H
#include <stdint.h>
#include <string.h>
#include "ZizhanAudioAbi.h"
#include "ZizhanAudioStream.h"
/* Host-verifiable service gate for the future zizhan system audio owner.
 * Ready is published only after control-page validation, real map, graph
 * START and a stable HAL producer watermark covering one full period.
 * This core does not build graphs, touch IOKit or enable output. */
enum {ZizhanServiceOff=0,ZizhanServiceStarting,ZizhanServiceReady,
      ZizhanServiceStopping,ZizhanServiceFailed};
typedef struct {
 uint32_t magic;unsigned phase,graph_started,map_held,reason;
 volatile ZizhanAudioControl *control;ZizhanAudioStream stream;
} ZizhanAudioService;
static inline void zizhan_service_fail(ZizhanAudioService *s,unsigned reason){
 if(!s){return;}s->reason=reason;s->phase=ZizhanServiceFailed;
 if(s->control)s->control->state&=~(ZizhanAudioOutputReady|ZizhanAudioOutputRunning);
}
static inline int zizhan_service_init(ZizhanAudioService *s,volatile ZizhanAudioControl *control,
                                      uint64_t base,uint32_t handle,unsigned count,uint64_t start_frame,uint64_t now){
 if(!s||s->magic||!zizhan_audio_control_valid(control))return 0;
 memset(s,0,sizeof *s);
 if(!zizhan_stream_init(&s->stream,base,handle,count,ZIZHAN_AUDIO_RING_FRAMES,start_frame,now))return 0;
 s->magic=0x5a415356u;s->phase=ZizhanServiceStarting;s->control=control;return 1;
}
static inline int zizhan_service_map(ZizhanAudioService *s,int held){
 if(!s||s->magic!=0x5a415356u||s->phase!=ZizhanServiceStarting)return 0;
 s->map_held=held?1u:0u;return 1;
}
static inline int zizhan_service_graph_started(ZizhanAudioService *s){
 if(!s||s->magic!=0x5a415356u||s->phase!=ZizhanServiceStarting||!s->map_held)return 0;
 s->graph_started=1;return 1;
}
static inline int zizhan_service_publish_ready(ZizhanAudioService *s){
 if(!s||s->magic!=0x5a415356u||s->phase!=ZizhanServiceStarting||!s->graph_started||!s->map_held||
    !zizhan_audio_has_frames(s->control,s->stream.next_frame,ZIZHAN_AUDIO_PERIOD_FRAMES))return 0;
 s->control->state|=ZizhanAudioOutputReady;s->phase=ZizhanServiceReady;return 1;
}
static inline int zizhan_service_stage(ZizhanAudioService *s,const int16_t *ring,unsigned slot,uint8_t *dst){
 if(!s||s->magic!=0x5a415356u||s->phase!=ZizhanServiceReady)return 0;
 uint64_t written;if(!zizhan_audio_written_stable(s->control,&written)){zizhan_service_fail(s,2);return 0;}
 if(!zizhan_stream_stage(&s->stream,ring,written,slot,dst)){
  s->reason=ZizhanStreamUnderrun;return 0;
 }
 return 1;
}
static inline unsigned zizhan_service_commit(ZizhanAudioService *s,unsigned slot,uint64_t now,uint8_t out[512]){
 if(!s||s->magic!=0x5a415356u||s->phase!=ZizhanServiceReady)return 0;
 const unsigned n=zizhan_stream_commit(&s->stream,slot,now,out);
 if(!n)zizhan_service_fail(s,3);
 return n;
}
static inline int zizhan_service_receive(ZizhanAudioService *s,const uint8_t *data,unsigned bytes,uint64_t now){
 if(!s||s->magic!=0x5a415356u)return 0;
 const int result=zizhan_stream_receive(&s->stream,data,bytes,now);
 if(result<0)zizhan_service_fail(s,4);
 return result;
}
static inline void zizhan_service_stop(ZizhanAudioService *s){
 if(!s||s->magic!=0x5a415356u)return;
 zizhan_stream_stop(&s->stream);
 s->control->state&=~(ZizhanAudioOutputReady|ZizhanAudioOutputRunning);
 s->phase=s->stream.phase==ZizhanStreamFailed?ZizhanServiceFailed:ZizhanServiceStopping;
}
static inline int zizhan_service_done(const ZizhanAudioService *s){
 return s&&s->magic==0x5a415356u&&zizhan_stream_done(&s->stream);
}
#endif
