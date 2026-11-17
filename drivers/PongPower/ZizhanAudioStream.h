/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AUDIO_STREAM_H
#define ZIZHAN_AUDIO_STREAM_H
#include <stdint.h>
#include <string.h>
#include "ZizhanAudioRing.h"
#include "ZizhanAudioBatch.h"
/* Host-verified finite producer bridge: a 48k/2ch int16 CoreAudio-style
 * ring is packed into one 2880-byte slot and submitted through the finite
 * WRITE/DONE batch. It never resamples, invents a channel or fills a
 * missing HAL period with silence. One instance per ring/mapping epoch. */
enum {ZizhanStreamReady=1,ZizhanStreamStaged,ZizhanStreamWaiting,ZizhanStreamDone,ZizhanStreamFailed};
enum {ZizhanStreamBad=1,ZizhanStreamUnderrun=2,ZizhanStreamBatch=3};
typedef struct {
 uint32_t magic;unsigned ring_frames,phase,underruns,reason;
 uint64_t next_frame;ZizhanAudioBatch batch;
} ZizhanAudioStream;
static inline int zizhan_stream_init(ZizhanAudioStream *s,uint64_t base,uint32_t handle,
                                     unsigned count,unsigned ring_frames,uint64_t start_frame,uint64_t now){
 if(!s||s->magic){return 0;}memset(s,0,sizeof *s);
 if(ring_frames!=ZizhanRingFrames||!count||count>4)return 0;
 if(!zizhan_batch_init_ex(&s->batch,base,handle,65536,ZizhanRingPcmBytes,ZizhanRingPcmFrames,count,now))return 0;
 s->magic=0x5a535452u;s->ring_frames=ring_frames;s->next_frame=start_frame;s->phase=ZizhanStreamReady;return 1;
}
static inline int zizhan_stream_stage(ZizhanAudioStream *s,const int16_t *ring,uint64_t produced_frames,
                                      unsigned slot,uint8_t *dst){
 if(!s||s->magic!=0x5a535452u||s->phase!=ZizhanStreamReady||!ring||!dst||slot!=s->batch.submitted)return 0;
 ZizhanRingReceipt r;if(!zizhan_ring_ready(produced_frames,s->next_frame,&r)){++s->underruns;s->reason=ZizhanStreamUnderrun;return 0;}
 if(!zizhan_ring_pack(ring,s->ring_frames,s->next_frame,dst,&r)){s->reason=ZizhanStreamBad;s->phase=ZizhanStreamFailed;return 0;}
 s->phase=ZizhanStreamStaged;return 1;
}
static inline unsigned zizhan_stream_commit(ZizhanAudioStream *s,unsigned slot,uint64_t now,uint8_t out[512]){
 if(!s||s->magic!=0x5a535452u||s->phase!=ZizhanStreamStaged||slot!=s->batch.submitted)return 0;
 if(!zizhan_batch_prepared(&s->batch,slot,ZizhanRingPcmBytes)){s->reason=ZizhanStreamBad;s->phase=ZizhanStreamFailed;return 0;}
 const unsigned n=zizhan_batch_next(&s->batch,now,out);
 if(!n){s->reason=ZizhanStreamBatch;s->phase=ZizhanStreamFailed;return 0;}
 s->next_frame+=ZizhanRingPcmFrames;s->phase=ZizhanStreamReady;return n;
}
static inline int zizhan_stream_receive(ZizhanAudioStream *s,const uint8_t *data,unsigned bytes,uint64_t now){
 if(!s||s->magic!=0x5a535452u)return 0;
 const int result=zizhan_batch_receive(&s->batch,data,bytes,now);
 if(result<0){s->reason=ZizhanStreamBatch;s->phase=ZizhanStreamFailed;}
 return result;
}
static inline void zizhan_stream_stop(ZizhanAudioStream *s){
 if(!s||s->magic!=0x5a535452u){return;}zizhan_batch_stop(&s->batch);
 if(s->phase!=ZizhanStreamFailed)s->phase=ZizhanStreamDone;
}
static inline int zizhan_stream_done(const ZizhanAudioStream *s){
 return s&&s->magic==0x5a535452u&&zizhan_batch_buffers_returned(&s->batch);
}
#endif
