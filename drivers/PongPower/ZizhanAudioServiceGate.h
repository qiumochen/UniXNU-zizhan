/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_SERVICE_GATE_H
#define ZIZHAN_AUDIO_SERVICE_GATE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "ZizhanAudioAbi.h"
/* I1 (2026-11-18): the decisions of the default-off ZizhanAudio IOKit owner
 * (ZizhanAudioService.cpp), kept in plain C so the host tests cover them.
 * zizhan_audio_service=1 publishes the service, the v2 control page and the
 * 48k/2ch int16 ring only: no graph, no Ready, no AMP/BST/TX/data pin.
 * StartOutput stays refused until the graph wiring lands. */
#define ZIZHAN_AUDIO_SERVICE_CLASS "ZizhanAudio"
#define ZIZHAN_AUDIO_SERVICE_CONTROL_BYTES 4096u
#define ZIZHAN_AUDIO_SERVICE_RING_BYTES (ZIZHAN_AUDIO_RING_FRAMES*ZIZHAN_AUDIO_OUT_CHANNELS*2u)
enum {ZizhanAudioMemoryControl=0,ZizhanAudioMemoryOutput=1};
enum {ZizhanAudioStartOutput=0,ZizhanAudioStopOutput=1,ZizhanAudioSetHostClock=4};   /* 4: one scalar, see ShengAudioHostClock.h */
enum {ZizhanGateOff=0,ZizhanGateStart,ZizhanGateBadValue,ZizhanGateConflictPong,ZizhanGateConflictPrm};
enum {ZizhanRequestOk=0,ZizhanRequestBadArgument,ZizhanRequestUnsupported,ZizhanRequestNotReady};
/* PRM mode 40 (ZizhanAudioPrm.cpp) is the graph owner that serves this
 * service: it waits for StartOutput, then runs the verified mode-38 graph on
 * the service ring with the HAL (or the silent self-test) as the producer. */
#define ZIZHAN_AUDIO_PRM_SERVICE_MODE 40u
/* PRM mode 41: mode 40 plus the profile 19 dual-amp playback window (attended, or dry). */
#define ZIZHAN_AUDIO_PRM_PLAY_MODE 41u
static inline int zizhan_service_owner_mode(unsigned prm){return prm==ZIZHAN_AUDIO_PRM_SERVICE_MODE||prm==ZIZHAN_AUDIO_PRM_PLAY_MODE;}
/* Only an explicit zizhan_audio_service=1 starts the owner. The Pong TFA9873
 * path (pong_audio) and every other PRM diagnostic mode own the same ADSP
 * graph and DMA window, so either one being set skips the service. */
static inline unsigned zizhan_service_gate(int has_service,unsigned service,int has_pong,unsigned pong_audio,
                                           int has_prm,unsigned prm){
 if(!has_service||service==0)return ZizhanGateOff;
 if(service!=1)return ZizhanGateBadValue;
 if(has_pong&&pong_audio!=0)return ZizhanGateConflictPong;
 if(has_prm&&prm!=0&&!zizhan_service_owner_mode(prm))return ZizhanGateConflictPrm;
 return ZizhanGateStart;
}
/* hz is the host counter frequency; ticks_per_frame_q32 = hz * 2^32 / 48000. */
static inline int zizhan_service_control_init(volatile ZizhanAudioControl *c,size_t bytes,uint64_t hz,uint64_t anchor){
 if(!c||bytes<sizeof(ZizhanAudioControl)||!hz||hz>(UINT64_MAX>>32))return 0;
 memset((void *)c,0,bytes);
 c->magic=ZIZHAN_AUDIO_MAGIC;c->version=ZIZHAN_AUDIO_VERSION;c->rate=ZIZHAN_AUDIO_RATE;
 c->out_channels=ZIZHAN_AUDIO_OUT_CHANNELS;c->in_channels=ZIZHAN_AUDIO_IN_CHANNELS;
 c->period_frames=ZIZHAN_AUDIO_PERIOD_FRAMES;c->ring_frames=ZIZHAN_AUDIO_RING_FRAMES;
 c->lead_frames=ZIZHAN_AUDIO_LEAD_FRAMES;c->ticks_per_frame_q32=(hz<<32)/ZIZHAN_AUDIO_RATE;
 c->out_anchor_host=anchor;
 return zizhan_audio_control_valid(c);
}
static inline int zizhan_service_memory_type(unsigned type){
 return type==ZizhanAudioMemoryControl||type==ZizhanAudioMemoryOutput;
}
/* Start/Stop take no arguments. Stop is always accepted (idempotent);
 * Start succeeds only once a graph owner exists. */
static inline unsigned zizhan_service_request(unsigned selector,unsigned scalar_in,unsigned struct_in,
                                              unsigned scalar_out,unsigned struct_out,int graph_available){
 if(scalar_in||struct_in||scalar_out||struct_out)return ZizhanRequestBadArgument;
 if(selector==ZizhanAudioStartOutput)return graph_available?ZizhanRequestOk:ZizhanRequestNotReady;
 if(selector==ZizhanAudioStopOutput)return ZizhanRequestOk;
 return ZizhanRequestUnsupported;
}
/* One Start per boot (I4, finite): the service requests, the PRM mode-40
 * worker takes the request, runs, and finishes. A second Start, or a Start
 * with no owner, is refused. Stop is a request the owner honours between
 * periods; Stop with nothing running is a no-op. */
enum {ZizhanHandoffIdle=0,ZizhanHandoffRequested,ZizhanHandoffRunning,ZizhanHandoffDone,ZizhanHandoffFailed};
typedef struct {volatile uint32_t phase,owner,stop,starts,refused;} ZizhanAudioHandoff;
static inline int zizhan_handoff_register(ZizhanAudioHandoff *h){
 if(!h){return 0;}uint32_t expected=0;
 return __atomic_compare_exchange_n(&h->owner,&expected,1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static inline int zizhan_handoff_request_start(ZizhanAudioHandoff *h){
 if(!h){return 0;}uint32_t expected=ZizhanHandoffIdle;
 if(!__atomic_load_n(&h->owner,__ATOMIC_ACQUIRE)||
    !__atomic_compare_exchange_n(&h->phase,&expected,(uint32_t)ZizhanHandoffRequested,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
  __atomic_add_fetch(&h->refused,1u,__ATOMIC_RELAXED);return 0;}
 __atomic_add_fetch(&h->starts,1u,__ATOMIC_RELAXED);return 1;
}
static inline void zizhan_handoff_request_stop(ZizhanAudioHandoff *h){
 if(h)__atomic_store_n(&h->stop,1u,__ATOMIC_RELEASE);
}
static inline int zizhan_handoff_take(ZizhanAudioHandoff *h){
 if(!h){return 0;}uint32_t expected=ZizhanHandoffRequested;
 return __atomic_compare_exchange_n(&h->phase,&expected,(uint32_t)ZizhanHandoffRunning,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static inline int zizhan_handoff_stopping(const ZizhanAudioHandoff *h){
 return h&&__atomic_load_n(&h->stop,__ATOMIC_ACQUIRE);
}
static inline void zizhan_handoff_finish(ZizhanAudioHandoff *h,int ok){
 if(h)__atomic_store_n(&h->phase,ok?(uint32_t)ZizhanHandoffDone:(uint32_t)ZizhanHandoffFailed,__ATOMIC_RELEASE);
}
/* Mode-40 start frame: the first whole period at or after the first stable
 * watermark the owner sees. The HAL writes forward from there, so every frame
 * the owner claims was written after that observation; earlier ring frames
 * (zeroed at publish, or a partial first write) are never claimed as media. */
static inline uint64_t zizhan_service_start_frame(uint64_t written){
 const uint64_t rest=written%ZIZHAN_AUDIO_PERIOD_FRAMES;
 if(!rest)return written;
 if(written>UINT64_MAX-ZIZHAN_AUDIO_PERIOD_FRAMES)return UINT64_MAX-(UINT64_MAX%ZIZHAN_AUDIO_PERIOD_FRAMES);
 return written+(ZIZHAN_AUDIO_PERIOD_FRAMES-rest);
}
#endif
