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
enum {ZizhanAudioStartOutput=0,ZizhanAudioStopOutput=1};
enum {ZizhanGateOff=0,ZizhanGateStart,ZizhanGateBadValue,ZizhanGateConflictPong,ZizhanGateConflictPrm};
enum {ZizhanRequestOk=0,ZizhanRequestBadArgument,ZizhanRequestUnsupported,ZizhanRequestNotReady};
/* Only an explicit zizhan_audio_service=1 starts the owner. The Pong TFA9873
 * path (pong_audio) and the PRM diagnostics (zizhan_audio_prm) own the same
 * ADSP graph and DMA window, so either one being set skips the service. */
static inline unsigned zizhan_service_gate(int has_service,unsigned service,int has_pong,unsigned pong_audio,
                                           int has_prm,unsigned prm){
 if(!has_service||service==0)return ZizhanGateOff;
 if(service!=1)return ZizhanGateBadValue;
 if(has_pong&&pong_audio!=0)return ZizhanGateConflictPong;
 if(has_prm&&prm!=0)return ZizhanGateConflictPrm;
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
#endif
