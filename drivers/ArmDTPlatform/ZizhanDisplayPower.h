/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_DISPLAY_POWER_H
#define ZIZHAN_DISPLAY_POWER_H
#include <stddef.h>
#include <stdint.h>

/* POWER-02 S1 request state machine. Pure bookkeeping: no DSI, DPU,
 * backlight or touch access, no kernel API and no allocation. The XNU
 * kFBMethodRequestPowerChange case will call zizhan_display_request() with
 * the raw scalar arguments; S2/S3 later call zizhan_display_complete() with
 * the sequence returned here.
 *
 * The 2026-11-18 device trace established the encoding: selector 12 carries
 * one scalar, in[0]==1 is on, in[0]==0 is off, and no in[1] is sent.
 * zizhan_display_classify() is the single rule that maps it; unknown values
 * are rejected, never guessed.
 *
 * Queue: up to four pending target requests, FIFO. A request for the same
 * target as the last pending one (or the current target when the queue is
 * empty) is a no-op; a different target is a follow-up and is accepted until
 * the queue is full. A full queue rejects and counts; nothing is overwritten.
 * "Busy" is set by S2/S3 while a device-level transaction is active
 * (zizhan_display_set_busy); S1 itself uses the queue bound and does not
 * silently reorder requests.
 *
 * Completion: complete(sequence, ok) frees the slot. On success the stable
 * target becomes the completed target; on failure the stable target is kept,
 * the sequence and reason are retained for logs, and nothing is retried.
 * Out-of-order completion is allowed; a duplicate/unknown sequence is
 * rejected without changing state. The sequence is a uint32_t and wraps; a
 * new sequence never collides with an active request. */

enum {
 ZizhanDisplayQueue=4u,
 ZizhanDisplayEvents=8u,
 ZizhanDisplayMaxScalars=8u
};

typedef enum {
 ZizhanDisplayTargetUnknown=0,
 ZizhanDisplayTargetOn=1,
 ZizhanDisplayTargetOff=2
} ZizhanDisplayTarget;

typedef enum {
 ZizhanDisplayStateOff=0,
 ZizhanDisplayStateOn=1,
 ZizhanDisplayStateTransitioning=2
} ZizhanDisplayState;

typedef enum {
 ZizhanDisplayAccepted=0,
 ZizhanDisplayNoOp=1,
 ZizhanDisplayRejectedMalformed=2,
 ZizhanDisplayRejectedUnknownTarget=3,
 ZizhanDisplayRejectedBusy=4,      /* reserved: no S1 path sets this */
 ZizhanDisplayRejectedQueueFull=5,
 ZizhanDisplayRejectedCompletion=6
} ZizhanDisplayResult;

typedef enum {
 ZizhanDisplayFailureNone=0,
 ZizhanDisplayFailureMalformed=1,
 ZizhanDisplayFailureUnknownTarget=2,
 ZizhanDisplayFailureQueueFull=3,
 ZizhanDisplayFailureCompletionRejected=4,
 ZizhanDisplayFailureCompletionFailed=5,
 ZizhanDisplayFailureBusy=6
} ZizhanDisplayFailure;

typedef enum {
 ZizhanDisplayPhaseRequest=0,
 ZizhanDisplayPhaseAccept=1,
 ZizhanDisplayPhaseNoOp=2,
 ZizhanDisplayPhaseReject=3,
 ZizhanDisplayPhaseCompleteOk=4,
 ZizhanDisplayPhaseCompleteFail=5
} ZizhanDisplayPhase;

typedef struct {
 uint32_t sequence;
 uint8_t has_sequence;
 uint8_t phase;
 uint8_t ok;
 uint8_t target;
 uint64_t time_us;
} ZizhanDisplayEvent;

typedef struct {
 uint32_t sequence;
 uint64_t time_us;
 ZizhanDisplayTarget target;
 ZizhanDisplayState before;
 size_t structure_bytes;
 uint8_t used;
} ZizhanDisplayRequest;

typedef struct {
 ZizhanDisplayResult result;
 ZizhanDisplayTarget target;
 ZizhanDisplayState before,after;
 ZizhanDisplayFailure failure;
 uint32_t sequence;
 uint8_t has_sequence;
 uint8_t ok;
 unsigned queue_depth;
 size_t structure_bytes;
} ZizhanDisplayReceipt;

typedef struct {
 uint32_t next_sequence;
 ZizhanDisplayState state;      /* reported state: On/Off/Transitioning */
 ZizhanDisplayState stable;     /* last confirmed On/Off */
 uint8_t transaction_busy;      /* set by S2/S3 while a device transaction runs */
 ZizhanDisplayRequest queue[ZizhanDisplayQueue];
 unsigned queued;
 uint32_t requests,accepted,noops,rejected,completed_ok,completed_fail;
 uint32_t rejected_malformed,rejected_unknown,rejected_queue_full,rejected_busy,completion_rejected;
 ZizhanDisplayFailure last_failure;
 uint32_t last_failure_sequence;
 ZizhanDisplayEvent events[ZizhanDisplayEvents];
 unsigned event_count,event_next;
} ZizhanDisplayPower;

/* 22H374 encoding, confirmed by the 2026-11-18 device evidence: selector 12
 * sends exactly one scalar; in[0]==1 means on, in[0]==0 means off, and no
 * in[1] is sent. Anything else is unknown. Do not add guesses for in[1]. */
static inline ZizhanDisplayTarget zizhan_display_classify(uint32_t count,const uint64_t *in){
 if(!in||count==0)return ZizhanDisplayTargetUnknown;
 if(in[0]==1)return ZizhanDisplayTargetOn;
 if(in[0]==0)return ZizhanDisplayTargetOff;
 return ZizhanDisplayTargetUnknown;
}

static inline void zizhan_display_init(ZizhanDisplayPower *p,ZizhanDisplayState initial){
 if(!p)return;
 p->next_sequence=0;
 p->state=initial==ZizhanDisplayStateOn?ZizhanDisplayStateOn:ZizhanDisplayStateOff;
 p->stable=p->state;
 p->transaction_busy=0;
 for(unsigned i=0;i<ZizhanDisplayQueue;++i){
  p->queue[i].sequence=0;p->queue[i].time_us=0;p->queue[i].target=ZizhanDisplayTargetUnknown;
  p->queue[i].before=p->stable;p->queue[i].structure_bytes=0;p->queue[i].used=0;
 }
 p->queued=0;p->requests=0;p->accepted=0;p->noops=0;p->rejected=0;
 p->completed_ok=0;p->completed_fail=0;p->rejected_malformed=0;
 p->rejected_unknown=0;p->rejected_queue_full=0;p->rejected_busy=0;p->completion_rejected=0;
 p->last_failure=ZizhanDisplayFailureNone;p->last_failure_sequence=0;
 for(unsigned i=0;i<ZizhanDisplayEvents;++i){
  p->events[i].sequence=0;p->events[i].has_sequence=0;p->events[i].phase=0;
  p->events[i].ok=0;p->events[i].target=0;p->events[i].time_us=0;
 }
 p->event_count=0;p->event_next=0;
}

static inline void zizhan_display_event(ZizhanDisplayPower *p,uint32_t sequence,
                                        uint8_t has_sequence,ZizhanDisplayPhase phase,
                                        int ok,ZizhanDisplayTarget target,uint64_t time_us){
 ZizhanDisplayEvent *e=&p->events[p->event_next];
 e->sequence=sequence;e->has_sequence=has_sequence;e->phase=(uint8_t)phase;
 e->ok=ok?1u:0u;e->target=(uint8_t)target;e->time_us=time_us;
 p->event_next=(p->event_next+1u)%ZizhanDisplayEvents;
 if(p->event_count<ZizhanDisplayEvents)++p->event_count;
}

static inline const ZizhanDisplayEvent *zizhan_display_last_event(const ZizhanDisplayPower *p,
                                                                  unsigned back){
 if(!p||back>=p->event_count)return 0;
 const unsigned index=(p->event_next+ZizhanDisplayEvents-1u-back)%ZizhanDisplayEvents;
 return &p->events[index];
}

static inline int zizhan_display_find(const ZizhanDisplayPower *p,uint32_t sequence){
 for(unsigned i=0;i<p->queued;++i)if(p->queue[i].used&&p->queue[i].sequence==sequence)return (int)i;
 return -1;
}

/* S2/S3 set this while a panel/DPU/DSI transaction is in flight. Requests
 * arriving then are rejected as Busy, not queued or reordered. */
static inline void zizhan_display_set_busy(ZizhanDisplayPower *p,int busy){
 if(p)p->transaction_busy=busy?1u:0u;
}

/* Next sequence, skipping any value held by an active request. The caller
 * must have a free queue slot, so the loop finds one within the queue size. */
static inline uint32_t zizhan_display_next_sequence(ZizhanDisplayPower *p){
 for(unsigned n=0;n<ZizhanDisplayQueue;++n){
  ++p->next_sequence;
  if(zizhan_display_find(p,p->next_sequence)<0)return p->next_sequence;
 }
 return ++p->next_sequence; /* unreachable with a free slot; still unique-ish */
}

static inline ZizhanDisplayReceipt zizhan_display_request(ZizhanDisplayPower *p,uint32_t count,
                                                           const uint64_t *in,size_t structure_bytes,
                                                           uint64_t time_us){
 ZizhanDisplayReceipt r;
 r.result=ZizhanDisplayRejectedMalformed;r.target=ZizhanDisplayTargetUnknown;
 r.before=ZizhanDisplayStateOff;r.after=ZizhanDisplayStateOff;
 r.failure=ZizhanDisplayFailureMalformed;r.sequence=0;r.has_sequence=0;r.ok=0;
 r.queue_depth=0;r.structure_bytes=structure_bytes;
 if(!p)return r;
 r.before=p->state;r.after=p->state;r.queue_depth=p->queued;
 ++p->requests;
 zizhan_display_event(p,0,0,ZizhanDisplayPhaseRequest,0,ZizhanDisplayTargetUnknown,time_us);
 if(count==0||(count&&!in)||count>ZizhanDisplayMaxScalars){
  ++p->rejected;++p->rejected_malformed;
  p->last_failure=ZizhanDisplayFailureMalformed;p->last_failure_sequence=0;
  zizhan_display_event(p,0,0,ZizhanDisplayPhaseReject,0,ZizhanDisplayTargetUnknown,time_us);
  return r;
 }
 const ZizhanDisplayTarget target=zizhan_display_classify(count,in);
 r.target=target;
 if(target==ZizhanDisplayTargetUnknown){
  ++p->rejected;++p->rejected_unknown;
  p->last_failure=ZizhanDisplayFailureUnknownTarget;p->last_failure_sequence=0;
  r.result=ZizhanDisplayRejectedUnknownTarget;
  zizhan_display_event(p,0,0,ZizhanDisplayPhaseReject,0,target,time_us);
  return r;
 }
 if(p->transaction_busy){
  ++p->rejected;++p->rejected_busy;
  p->last_failure=ZizhanDisplayFailureBusy;p->last_failure_sequence=0;
  r.result=ZizhanDisplayRejectedBusy;
  zizhan_display_event(p,0,0,ZizhanDisplayPhaseReject,0,target,time_us);
  return r;
 }
 const ZizhanDisplayTarget last=p->queued?p->queue[p->queued-1].target:
   (p->stable==ZizhanDisplayStateOn?ZizhanDisplayTargetOn:ZizhanDisplayTargetOff);
 if(target==last){
  ++p->noops;r.result=ZizhanDisplayNoOp;r.failure=ZizhanDisplayFailureNone;
  r.after=p->state;r.queue_depth=p->queued;
  zizhan_display_event(p,0,0,ZizhanDisplayPhaseNoOp,1,target,time_us);
  return r;
 }
 if(p->queued>=ZizhanDisplayQueue){
  ++p->rejected;++p->rejected_queue_full;
  p->last_failure=ZizhanDisplayFailureQueueFull;p->last_failure_sequence=0;
  r.result=ZizhanDisplayRejectedQueueFull;
  zizhan_display_event(p,0,0,ZizhanDisplayPhaseReject,0,target,time_us);
  return r;
 }
 const uint32_t sequence=zizhan_display_next_sequence(p);
 ZizhanDisplayRequest *q=&p->queue[p->queued++];
 q->sequence=sequence;q->time_us=time_us;q->target=target;q->before=p->stable;
 q->structure_bytes=structure_bytes;q->used=1;
 p->state=ZizhanDisplayStateTransitioning;
 ++p->accepted;
 r.result=ZizhanDisplayAccepted;r.failure=ZizhanDisplayFailureNone;
 r.sequence=sequence;r.has_sequence=1;r.before=q->before;r.after=p->state;
 r.queue_depth=p->queued;
 zizhan_display_event(p,sequence,1,ZizhanDisplayPhaseAccept,1,target,time_us);
 return r;
}

/* Complete an accepted sequence. Returns 1 when the record was found and
 * consumed, 0 when it was unknown or already completed. Failure keeps the
 * stable target (or the prior stable state when the queue drains) and records
 * the sequence/reason; it never retries or fabricates a successful off. */
static inline int zizhan_display_complete(ZizhanDisplayPower *p,uint32_t sequence,int ok,
                                          ZizhanDisplayReceipt *out){
 ZizhanDisplayReceipt r;
 r.result=ZizhanDisplayRejectedCompletion;r.target=ZizhanDisplayTargetUnknown;
 r.before=ZizhanDisplayStateOff;r.after=ZizhanDisplayStateOff;
 r.failure=ZizhanDisplayFailureCompletionRejected;r.sequence=sequence;r.has_sequence=1;
 r.ok=0;r.queue_depth=0;r.structure_bytes=0;
 if(out)*out=r;
 if(!p)return 0;
 const int index=zizhan_display_find(p,sequence);
 if(index<0){
  ++p->completion_rejected;
  p->last_failure=ZizhanDisplayFailureCompletionRejected;p->last_failure_sequence=sequence;
  zizhan_display_event(p,sequence,1,ZizhanDisplayPhaseReject,0,ZizhanDisplayTargetUnknown,0);
  return 0;
 }
 ZizhanDisplayRequest done=p->queue[index];
 r.result=ZizhanDisplayAccepted;
 for(unsigned i=(unsigned)index;i+1u<p->queued;++i)p->queue[i]=p->queue[i+1u];
 --p->queued;
 p->queue[p->queued].used=0;
 if(ok){
  p->stable=done.target==ZizhanDisplayTargetOn?ZizhanDisplayStateOn:ZizhanDisplayStateOff;
  ++p->completed_ok;
  p->state=p->queued?ZizhanDisplayStateTransitioning:p->stable;
  zizhan_display_event(p,sequence,1,ZizhanDisplayPhaseCompleteOk,1,done.target,done.time_us);
  r.result=ZizhanDisplayAccepted;r.failure=ZizhanDisplayFailureNone;r.ok=1;
 }else{
  ++p->completed_fail;
  p->last_failure=ZizhanDisplayFailureCompletionFailed;p->last_failure_sequence=sequence;
  p->state=p->queued?ZizhanDisplayStateTransitioning:p->stable;
  zizhan_display_event(p,sequence,1,ZizhanDisplayPhaseCompleteFail,0,done.target,done.time_us);
  r.failure=ZizhanDisplayFailureCompletionFailed;
 }
 r.target=done.target;r.before=done.before;r.after=p->state;
 r.queue_depth=p->queued;r.structure_bytes=done.structure_bytes;
 if(out)*out=r;
 return 1;
}

#endif
