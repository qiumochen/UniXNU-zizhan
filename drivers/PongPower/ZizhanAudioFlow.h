/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_FLOW_H
#define ZIZHAN_AUDIO_FLOW_H
#include <stdint.h>
#include <string.h>
#include "ZizhanAudioWrite.h"
/* Continuous WR_SHMEM stream (2026-11-18, after the finite batch core):
 * up to four period slots 0x3000 apart in the 64 KiB DMA window, reused in
 * order, with at most `inflight` WRITEs outstanding. Every WRITE carries a
 * unique token, so a DONE for an earlier use of the same slot can never be
 * taken for the current one. A slot is reused only after its exact DONE.
 * Per-WRITE budget 100 ms; the whole stream is bounded by limit_us. The
 * core builds packets and tracks ownership only: the caller stages the
 * slot contents, proves the producer watermark, and owns map/graph/output.
 * A stopped or failed flow is never restarted (one instance per epoch). */
enum {ZizhanFlowRunning=1,ZizhanFlowDraining,ZizhanFlowDone,ZizhanFlowFailed};
enum {ZizhanFlowBad=1,ZizhanFlowTimeout=2,ZizhanFlowRemote=3,ZizhanFlowSend=4};
enum {ZizhanFlowSlots=4u,ZizhanFlowStride=0x3000u,ZizhanFlowTokenBase=0x7a2c0000u};
typedef struct {ZizhanWriteState write;uint64_t frame;unsigned busy;} ZizhanFlowSlot;
typedef struct {
 uint32_t magic,handle,next_token;unsigned phase,inflight,max_inflight,bytes,frames,next_slot,reason,uncertain;
 uint64_t base,began_us,limit_us,next_frame,submitted,completed,confirmed_frames,peak_us;
 unsigned peak_inflight;ZizhanFlowSlot slot[ZizhanFlowSlots];
} ZizhanAudioFlow;
static inline int zizhan_flow_init(ZizhanAudioFlow *f,uint64_t base,uint32_t handle,unsigned max_inflight,
                                   unsigned bytes,unsigned frames,uint64_t start_frame,uint64_t limit_us,uint64_t now){
 if(!f||f->magic){return 0;}memset(f,0,sizeof *f);
 if(!handle||!max_inflight||max_inflight>3u||!bytes||bytes>ZizhanFlowStride||(bytes&3u)||!frames||bytes%frames||
    (base>>32)!=1||!(uint32_t)base||(base&4095)||
    (uint64_t)(uint32_t)base+(ZizhanFlowSlots-1)*ZizhanFlowStride+bytes>0x100000000ull||
    !limit_us||limit_us>30000000ull)return 0;
 f->magic=0x5a464c57u;f->handle=handle;f->max_inflight=max_inflight;f->bytes=bytes;f->frames=frames;
 f->base=base;f->began_us=now;f->limit_us=limit_us;f->next_frame=start_frame;f->phase=ZizhanFlowRunning;return 1;
}
static inline int zizhan_flow_valid(const ZizhanAudioFlow *f){return f&&f->magic==0x5a464c57u;}
static inline void zizhan_flow_fail(ZizhanAudioFlow *f,unsigned reason){
 if(!zizhan_flow_valid(f)){return;}f->reason=reason;f->phase=ZizhanFlowFailed;
 for(unsigned i=0;i<ZizhanFlowSlots;++i)if(f->slot[i].busy)f->uncertain=1;
}
/* Next slot to stage, or -1: running, under the in-flight cap, slot free. */
static inline int zizhan_flow_claim(const ZizhanAudioFlow *f,uint64_t *frame){
 if(!zizhan_flow_valid(f)||f->phase!=ZizhanFlowRunning||f->inflight>=f->max_inflight||
    f->slot[f->next_slot].busy)return -1;
 if(frame){*frame=f->next_frame;}return (int)f->next_slot;
}
static inline uint64_t zizhan_flow_slot_address(const ZizhanAudioFlow *f,unsigned slot){
 return f->base+(uint64_t)ZizhanFlowStride*slot;
}
/* Build the WRITE for the claimed slot after the caller staged it. */
static inline unsigned zizhan_flow_submit(ZizhanAudioFlow *f,unsigned slot,uint64_t now,uint8_t out[512]){
 if(!zizhan_flow_valid(f)||!out||f->phase!=ZizhanFlowRunning||slot!=f->next_slot||f->slot[slot].busy||
    f->inflight>=f->max_inflight)return 0;
 ZizhanFlowSlot *s=&f->slot[slot];memset(&s->write,0,sizeof s->write);
 s->write.address=zizhan_flow_slot_address(f,slot);s->write.handle=f->handle;s->write.bytes=f->bytes;
 uint8_t body[44]={0};zizhan_write_put32(body,(uint32_t)s->write.address);zizhan_write_put32(body+4,(uint32_t)(s->write.address>>32));
 zizhan_write_put32(body+8,f->handle);zizhan_write_put32(body+12,f->bytes);
 GprPacket p;memset(&p,0,sizeof p);p.src_domain=3;p.dst_domain=2;p.src_port=ZizhanPcmPort;p.dst_port=ZizhanPcmShmem;
 p.token=ZizhanFlowTokenBase|(f->next_token&0xffffu);p.opcode=AR_OP_WRITE;
 const unsigned n=gpr_encode(out,512,&p,body,sizeof body);if(!n){zizhan_flow_fail(f,ZizhanFlowBad);return 0;}
 s->write.token=p.token;s->write.phase=ZizhanWriteWaiting;s->write.uncertain=1;s->write.began_us=now;s->write.sent=1;
 s->frame=f->next_frame;s->busy=1;++f->inflight;if(f->inflight>f->peak_inflight){f->peak_inflight=f->inflight;}
 ++f->next_token;++f->submitted;f->next_frame+=f->frames;f->next_slot=(slot+1)%ZizhanFlowSlots;return n;
}
static inline void zizhan_flow_send_failed(ZizhanAudioFlow *f){zizhan_flow_fail(f,ZizhanFlowSend);}
static inline void zizhan_flow_finish_if_drained(ZizhanAudioFlow *f){
 if(f->phase==ZizhanFlowDraining&&!f->inflight)f->phase=ZizhanFlowDone;
}
/* 1: a DONE returned a slot; 0: not ours or not a completion; -1: failure. */
static inline int zizhan_flow_receive(ZizhanAudioFlow *f,const uint8_t *data,unsigned bytes,uint64_t now){
 if(!zizhan_flow_valid(f)||!data||(f->phase!=ZizhanFlowRunning&&f->phase!=ZizhanFlowDraining))return 0;
 GprPacket p;if(gpr_decode(data,bytes,&p)!=GprOk){return 0;}
 for(unsigned i=0;i<ZizhanFlowSlots;++i){ZizhanFlowSlot *s=&f->slot[i];
  if(!s->busy||s->write.token!=p.token){continue;}
  const int r=zizhan_write_receive(&s->write,data,bytes);
  if(r<0){zizhan_flow_fail(f,ZizhanFlowRemote);return -1;}
  if(r==1){const uint64_t took=now>=s->write.began_us?now-s->write.began_us:0;if(took>f->peak_us){f->peak_us=took;}
   s->busy=0;--f->inflight;++f->completed;f->confirmed_frames+=f->frames;zizhan_flow_finish_if_drained(f);}
  return r;
 }
 return 0;
}
static inline void zizhan_flow_tick(ZizhanAudioFlow *f,uint64_t now){
 if(!zizhan_flow_valid(f)||f->phase==ZizhanFlowDone||f->phase==ZizhanFlowFailed)return;
 for(unsigned i=0;i<ZizhanFlowSlots;++i){const ZizhanFlowSlot *s=&f->slot[i];
  if(s->busy&&(now<s->write.began_us||now-s->write.began_us>=100000)){zizhan_flow_fail(f,ZizhanFlowTimeout);return;}}
 if(f->phase==ZizhanFlowRunning&&(now<f->began_us||now-f->began_us>=f->limit_us)){f->phase=ZizhanFlowDraining;zizhan_flow_finish_if_drained(f);}
}
/* Stop submitting; the flow is Done once every outstanding WRITE returned. */
static inline void zizhan_flow_stop(ZizhanAudioFlow *f){
 if(!zizhan_flow_valid(f)||f->phase!=ZizhanFlowRunning){return;}f->phase=ZizhanFlowDraining;zizhan_flow_finish_if_drained(f);
}
static inline int zizhan_flow_returned(const ZizhanAudioFlow *f){
 return zizhan_flow_valid(f)&&f->phase==ZizhanFlowDone&&!f->inflight&&!f->uncertain&&f->submitted==f->completed;
}
#endif
